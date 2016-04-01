// Hook functions are declared with extern "C", because we want to disable
// the C++ name mangling and make the instrumentation easier.
//

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <iostream>
#include <fstream>
#include <pthread.h>
#include <signal.h>
#include <stack>
#include <string>
#include <sstream>
#include <unistd.h>
#include <vector>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include <sched.h>// for yield()


#include "rcs/IDAssigner.h"

#include "dyn-aa/LogRecord.h"
#include "rtm.h"

using namespace std;
using namespace rcs;
using namespace neongoby;

//#define USE_BACKTRACE
#define USE_TX
//#define USE_TX_STATS
//#define USE_TX_STATS_MEM
#define USE_TSAN_CRT
//#define USE_SAMPLING

//#define SAMPLING_RATE 10

//#define EMPTY_TEST
//#define EMPTY_IF

/*
 * Dynamic Loop Cut, compared to original design
 * threshold is auto adjusted from small value
 */
#define _USE_DYNAMIC_LOOP_CUT_THRESHOLD_

#ifdef _USE_DYNAMIC_LOOP_CUT_THRESHOLD_
/*
 * linear adjustment for threshold
 */
#define _USE_LINEAR_THRESHOLD_ADJUSTMENT_
/*
 * exponential adjustment for threshold
 */
//#define _USE_EXPONENTIAL_THRESHOLD_ADJUSTMENT_
#endif

/*
 * Before starting new transaction,
 * consult pvector to see whether all threads are ready
 * this also helps to protect context for race detector
 * -----------------------
 * in most cases use safe sync(actually it is backoff)
 * is good for performance
 */
#define _USE_SAFE_SYNC_

/*
 * Thread local path selector
 * This selector is thread local, it is used to select whether a thread
 * should go for fast path or slow path
 */
#define USE_TL_SLOW_PATH_WHEN_CAP_ABRT
#define USE_TL_SLOW_PATH_WHEN_UNKNOWN_ABRT
#define USE_THREAD_LOCAL_PATH_SELECTOR
//#define USE_TL_SLOW_PATH_WHEN_TIMEOUT


#ifdef USE_TL_SLOW_PATH_WHEN_TIMEOUT
#ifndef USE_THREAD_LOCAL_PATH_SELECTOR
#define USE_THREAD_LOCAL_PATH_SELECTOR
#endif
#endif

#ifdef USE_TL_SLOW_PATH_WHEN_CAP_ABRT
#ifndef USE_THREAD_LOCAL_PATH_SELECTOR
#define USE_THREAD_LOCAL_PATH_SELECTOR
#endif
#endif

#ifdef USE_TL_SLOW_PATH_WHEN_UNKNOWN_ABRT
#ifndef USE_THREAD_LOCAL_PATH_SELECTOR
#define USE_THREAD_LOCAL_PATH_SELECTOR
#endif
#endif


#ifdef USE_BACKTRACE
#ifndef USE_G_LOCK
#define USE_G_LOCK
#endif
#endif

/*
 * the TX_DUMP_SELECTOR switch is used to selectively dump out lbr
 * on specific tx abort event
 */
#define TX_DUMP_SELECTOR (_XABORT_CAPACITY)
//#define TX_DUMP_SELECTOR (0)

/*
 * Dump Branch info when tx abort
 * requires liblbr
 */
//#define USE_DUMP_TXABRT_BRANCH

#ifdef USE_DUMP_TXABRT_BRANCH

#define USE_G_LOCK
#include <intel-lbr/lbr.h>

void set_cpu_affinity(int cpuid)
{
	cpu_set_t cpuset;
	pthread_t thread;

	thread = pthread_self();

	CPU_ZERO(&cpuset);
	CPU_SET(cpuid, &cpuset);
	int result = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);

	if (result)
	{
		fprintf(stderr, "set cpu affinity %d failed, use taskset instead\n", cpuid);
	}
}

#define LBR_DUMP_FILE_NAME "lbr.dump"

static ofstream lbr_dump_file;

#endif

/*
 * retry on explicitly abort,
 * usually explicitly aborted transaction should wait till all aborts get resolved.
 */
//#define USE_RETRY_ON_EXPLICIT_ABORT

/*
 * SOSP Counter is for terminating tx region when memops reaches some threshold
 * if current TX region was aborted due to capacity
 */
//#define USE_SOSP_COUNTER
//#define USE_SOSP_DYNAMIC_THRESHOLD



/*
 * dynamic prune search space
 * - iff there is only one thread running, we are sure it is race free
 */
//#define _DYNAMIC_PRUNE_


#ifdef USE_TX_STATS_MEM
#define MEM_HOOK_COUNTER_INC (__sync_fetch_and_add(&tx_stats.mem_hook_count,1))
//#define MEM_HOOK_COUNTER_INC (tx_stats.mem_hook_count++)
#endif

/*
 * optimize likely/unlikely conditions
 */
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)


#ifdef USE_BACKTRACE
#include <execinfo.h>

void print_trace (void)
{
	void *array[10];
	size_t size;

	char **strings;
	size_t i;

	size = backtrace (array, 10);
	strings = backtrace_symbols (array, size);

	printf ("Obtained %zd stack frames.\n", size);

	for (i = 0; i < size; i++)
		printf ("%s\n", strings[i]);

	free (strings);
}
#endif


enum {
	TXMODE_FASTTRACK = 0,
	TXMODE_HTM = 1,
	TXMODE_HTM_ABORTED = 2,
};


#ifdef USE_SAMPLING
#if 1
static unsigned int g_seed;
//Used to seed the generator.
inline void fast_srand( int seed )
{
	g_seed = seed;
}
//fastrand routine returns one integer, similar output value range as C lib.
inline int fastrand()
{
	g_seed = (214013 * g_seed + 2531011);
	return (g_seed >> 16) & 0x7FFF;
}
#else
static __inline__ unsigned long long rdtsc(void)
{
	unsigned hi, lo;
	__asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
	return ( (unsigned long long)lo) | ( ((unsigned long long)hi) << 32 );
}
#define fastrand rdtsc
#endif
#endif

typedef unsigned long uptr;


/////////////////////////////////////////////////////////
#ifdef USE_TX_STATS
struct tx_stats {
	unsigned int n_aborts;
	unsigned int n_unknown_aborts;
	unsigned int n_capacity_aborts;
	unsigned int n_success;
	/*
	 * tx detector start counter
	 */
	unsigned int detector_cnt;
	/*
	 * escaped tx region count
	 */
	unsigned int escaped_tx_count;
	/*
	 * memory wrapper invoke counter
	 */
	unsigned long long mem_hook_count;
};

__attribute__((aligned(64))) struct tx_stats tx_stats = {0,};
#endif
////////////////////////////////////////////////////////////////////
#ifdef USE_G_LOCK
#define LOCKED 1
#define UNLOCKED 0
bool glock = UNLOCKED;
void g_lock()
{
	for (; !__sync_bool_compare_and_swap(&glock, UNLOCKED, LOCKED);)
	{
		while (glock == LOCKED)
		{
			asm("nop");
			asm("nop");
			asm("nop");
		}
	}
//	printf("acquired lock\n");
}

/*
 * must hold the lock first
 */
void g_unlock()
{
	__sync_bool_compare_and_swap(&glock, LOCKED, UNLOCKED);
//	printf("released lock\n");
}
#endif
//////////////////////////////////////////////////////////////////////

/*
 * we are using amd64 platform, so sizeof(unsigned long)=8
 * the path_vector can hold state of 8*8/2=32 pairs threads.
 * path_vector is used to synchronize all tx region behaviours.
 * Since path_vector is read at the beginning of the ve
 */

typedef unsigned long t_vector;

#ifdef USE_DUMP_TXABRT_BRANCH
/*
 * when using LBR to dump call stack,
 * only MAX_CPU numbers of threads(including main) are supported
 */
#define THREAD_MAX (1<<3)
#define THREAD_MAX_MASK (THREAD_MAX-1)
#else
/*
 * when not profiling, we support up to 32 threads
 */
#define THREAD_MAX (1<<5)
#define THREAD_MAX_MASK (THREAD_MAX-1)
#endif
/*
 * initial state is TXMODE_HTM
 * we use only THREAD_MAX THREADS, so lower (THREAD_MAX)bit is masked to use
 */
#define PV_MASK_FAST_PATH 0x00000000FFFFFFFF
#define PV_FAST_PATH 0xFFFFFFFF

/*
 * pv is global path selector
 * pv is used both inside and outside of txregion, be careful!
 */
__attribute__((aligned(64))) t_vector pv = PV_FAST_PATH;

#define get_state(V,T) (((V)>>(T)) & 0x01)
#define set_state(V,T) __sync_or_and_fetch(V,1<<T)
#define clear_state(V,T) __sync_and_and_fetch(V,~(1<<T))


/////////////////////////////////////////////////////////////////////
/*
 * thread local info
 * actually not thread_local but some thread info for mapping tid to hashedtid
 */
typedef struct _thread_local_info
{
	pthread_t tid;
	int idx;
} thread_local_info;

__attribute__((aligned(64))) thread_local_info tli[THREAD_MAX] = {{0}};

/*
 * once thread idx is fixed, it is stored in thread local storage
 */
__thread int idx = -1;


#ifdef USE_THREAD_LOCAL_PATH_SELECTOR

/*
 * Thread local path selector
 */
#define TL_PATH_SLOW (0)
#define TL_PATH_FAST (1)
__thread int tlps = TL_PATH_FAST;

#endif

/*
 * counter for thread local memops
 */
#ifdef USE_SOSP_COUNTER
__thread unsigned long memops_cnt = 0;
#ifdef USE_SOSP_DYNAMIC_THRESHOLD
/*
 * dynamic THRESHOLD
 * if capacity abort resolved, increase it by half,
 * otherwise decrease it by half
 */
__thread unsigned long SOSP_COUNTER_THRESHOLD = (1024 * 1024 * 512);
#else
#define SOSP_COUNTER_THRESHOLD (1024*1024*512)
#endif//USE_SOSP_DYNAMIC_THRESHOLD
#endif


#ifdef _DYNAMIC_PRUNE_
/*
 * thread count, if thread count<=1, we do not start tx
 */

unsigned int thread_count;

#define THREAD_COUNT (thread_count)
#define INCREASE_THREAD_COUNT (__sync_fetch_and_add(&thread_count,1))
#define DECREASE_THREAD_COUNT (__sync_fetch_and_sub(&thread_count,1))

#endif

inline int t_hash(int x)
{
	return THREAD_MAX_MASK && ((x >> 4) + x);
}

inline thread_local_info* allocateThreadInfo()
{
	pthread_t tid = pthread_self();
	int i = t_hash(tid);
	for (;;)
	{
		if (__sync_bool_compare_and_swap(&tli[i].tid, 0, tid))
		{
			tli[i].idx = i;
			idx = i;//update thread local idx
#ifdef _DYNAMIC_PRUNE_
			INCREASE_THREAD_COUNT;
#endif

#ifdef USE_DUMP_TXABRT_BRANCH
			/*
			 * when new thread info is allocated<new threac is created>
			 * set it's affinity to cpu<idx>
			 * and start LBR
			 * TODO: need to limit the idx(<NUM_CPU) passed to start_lbr and set_cpu_affinity
			 */
			set_cpu_affinity(idx);
			if (start_lbr(idx))
			{
				fprintf(stderr, "Can not start LBR for thread %d\n", idx);
			}
#endif

			return &tli[i];
		}
		i++;
		i = i % THREAD_MAX;
	}
	return NULL;
}


inline bool freeThreadInfo()
{
	pthread_t tid = pthread_self();
	int hash_idx = t_hash(tid);
	int i = hash_idx;
	do {
		if (tli[i].tid == tid)
		{
			__sync_bool_compare_and_swap(&tli[i].tid, tid, 0);
#ifdef _DYNAMIC_PRUNE_
			DECREASE_THREAD_COUNT;
#endif

#ifdef USE_DUMP_TXABRT_BRANCH
			/*
			 * stop LBR when thread exit
			 */
			cleanup_lbr(idx);
#endif
			return true;
		}
		i++;
		i = i % THREAD_MAX;
	} while (i != hash_idx);
	return false;
}

inline thread_local_info* getThreadInfo()
{
	if (likely(idx != -1))
		return &tli[idx];
	pthread_t tid = pthread_self();
	int hash_idx = t_hash(tid);
	int i = hash_idx;
	do {
		if (tli[i].tid == tid)
		{
			return &tli[i];
		}
		i++;
		i = i % THREAD_MAX;
	} while (i != hash_idx);
	return allocateThreadInfo();
}

//#define getHashedTID() ((likely(idx!=-1))?idx:allocateThreadInfo()->idx)
#define getHashedTID() (idx)
/////////////////////////////////////////////////////
//For gettid()
#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>

int gettid()
{
	return syscall(SYS_gettid);
}
//////////////////////////////////////////////////////
/*
 * all threads wait here
 * If my flag is TXMODE_FASTTRACK, I need to reset it to TXMODE_HTM
 */
#define _WAIT_SLOW_PATH_ \
{ \
	while((pv&PV_MASK_FAST_PATH)!=PV_FAST_PATH) \
	{ \
		int toc=0xFFFFFF;\
		sched_yield(); \
		if(toc--==0) \
		{ \
			toc = 0xFFFFFF; \
			printf("tid:%d(htid=%d) pv=0x%lx Timeout!\n", gettid(),getHashedTID(), pv );\
		} \
		if(!get_state(pv,getHashedTID())) \
			set_state(&pv,getHashedTID()); \
	} \
};

__attribute__((__always_inline__)) inline void tx_begin(void)
{
	unsigned int status = 0;
	if (_xtest())
	{
		_xend();
	}
#ifdef _DYNAMIC_PRUNE_
	/*
	 * if there is only one thread we are sure it is race free.
	 */
	if (THREAD_COUNT < 1)
	{
		return;
	}
#endif

#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	/*
	 * reset thread local path to fast path
	 */
	tlps = TL_PATH_FAST;
#endif

#if defined(USE_SOSP_DYNAMIC_THRESHOLD) || defined(USE_RETRY_ON_EXPLICIT_ABORT)
retry_tx:
#endif

#ifdef _USE_SAFE_SYNC_
	/*
	 * FIXME: sched_yield may be a bad idea.
	 */
	int toc = 0xFFFFF;
	while ((pv & PV_MASK_FAST_PATH) != PV_FAST_PATH)
	{
		//if ((toc & 0xFFFF) == 0xFFFF)
		//	sched_yield();
		if (toc-- == 0)
		{
			//printf("-tid:%d(htid=%d) pv=0x%lx Timeout!\n", gettid(), getHashedTID(), pv );
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
#ifdef USE_TL_SLOW_PATH_WHEN_TIMEOUT
			/*
			 * timeout waiting. set local thread to slow path
			 */
			tlps = TL_PATH_SLOW;
			return;
#endif
#endif
			break;//go on
		}

	}
#else
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	/*
	 * timeout waiting. set local thread to slow path
	 */
	if ((pv & PV_MASK_FAST_PATH) != PV_FAST_PATH)
	{
		tlps = TL_PATH_SLOW;
		return;
	}
#endif
#endif
	////////////////////////////////////////////////////////////////////////
	if ((status = _xbegin()) == _XBEGIN_STARTED)
	{
		/*
		 * read global abort variable, if global_abort_variable has been raised
		 * we just abort explicitly, otherwise by reading global_abort_variable
		 * a shared data will be set. it will be used to trigger abort of other
		 * transaction.
		 */
		if (unlikely((pv & PV_MASK_FAST_PATH) != PV_FAST_PATH))
		{
			//_xabort(0xEE);
			_xabort(0x00);
		}
	} else {

#ifdef USE_DUMP_TXABRT_BRANCH
		/*
		 * dump LBR info when tx capacity abort
		 */
		if ((status & TX_DUMP_SELECTOR) == TX_DUMP_SELECTOR)
		{
			lbr_stack lbrstack;
			dump_lbr(idx, &lbrstack);
			g_lock();
			lbr_dump_file.write((char*)&lbrstack, sizeof(lbrstack));
			//inteprete_lbr_info(&lbrstack);
			//printf("------0x%x-----\n", status);
			g_unlock();
			start_lbr(idx);
		}
#endif

#ifdef USE_BACKTRACE
		if ((status & TX_DUMP_SELECTOR) == TX_DUMP_SELECTOR)
		{
			g_lock();
			print_trace();
			g_unlock();
		}
#endif

		//printf("tid:%d, aborted state=0x%x\n",gettid(),status);
#ifdef USE_TX_STATS
		tx_stats.n_aborts++;
#endif

		switch (status)
		{
		/*
		 * Unknown reason? just let it go
		 */
		case (0):
#ifdef USE_TX_STATS
			tx_stats.n_unknown_aborts++;
#endif

#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
#ifdef USE_TL_SLOW_PATH_WHEN_UNKNOWN_ABRT
			/*
			 * Unknown abort, set current thread to slow path
			 */
			tlps = TL_PATH_SLOW;
#endif
#endif

			break;

		/*
		 * Capacity abort
		 */
		case (_XABORT_CAPACITY):
#ifdef USE_TX_STATS
			tx_stats.n_capacity_aborts++;
#endif
#ifdef USE_SOSP_COUNTER
			/*
			 * Capacity abort, restart this Tx Region and Terminate
			 * when memops reaches some threshold
			 */
			memops_cnt = 0;
#ifdef USE_SOSP_DYNAMIC_THRESHOLD
			/*
			 * decrease threshold by half
			 */
			if (SOSP_COUNTER_THRESHOLD > 2)
				SOSP_COUNTER_THRESHOLD = SOSP_COUNTER_THRESHOLD / 2;
#endif
			/*
			 * restart current tx region
			 */
			goto retry_tx;
#else//USE_SOSP_COUNTER
			/*
			 * Capacity abort, just let it go
			 */
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
#ifdef USE_TL_SLOW_PATH_WHEN_CAP_ABRT
			/*
			 * Capacity abort, set current thread to slow path
			 */
			tlps = TL_PATH_SLOW;
#endif
#endif

#endif//USE_SOSP_COUNTER
			break;

		/*
		 * Explicitly abort
		 */
		case (_XABORT_EXPLICIT):
#ifdef USE_TX_STATS
			tx_stats.escaped_tx_count++;
#endif

#ifdef USE_RETRY_ON_EXPLICIT_ABORT
			goto retry_tx;
#else
			/*
			 * SLOWPATH
			 * should also go for slow path, although this thread may be the winner thread
			 * and in this situation, all tx-slow-path should be avoided
			 * And this always comes after XABORT_CONFLICT ( for global_abort_variable )
			 */
			//clear_state(&pv,getHashedTID());
#endif//USE_RETRY_ON_EXPLICIT_ABORT
			break;

		default:
			/*
			 * SLOWPATH
			 * Real conflict
			 * Start concurrency bug detector
			 */
#ifdef USE_TX_STATS
			if ((pv & PV_MASK_FAST_PATH) == PV_MASK_FAST_PATH)
			{
				/*
				 * I am the 1st thread to set this variable
				 * possilility that all threads here are loser,
				 * winner already escaped,
				 * but we also run detector in this case.
				 */

				tx_stats.detector_cnt++;
			}
#endif
			clear_state(&pv, getHashedTID()); //set to TXMODE_FASTTRACK)
			break;
		}
	}
}

__attribute__((__always_inline__)) inline void tx_end(void)
{
	if (_xtest())
	{
		_xend();
#ifdef USE_TX_STATS
		tx_stats.n_success++;
#endif
	}
}

#ifdef USE_SOSP_COUNTER

void sosp_logic()
{
	memops_cnt++;
	if (memops_cnt > SOSP_COUNTER_THRESHOLD)
	{
		tx_end();
		memops_cnt = 0;
#ifdef USE_SOSP_DYNAMIC_THRESHOLD
		/*
		 * increase threshold by half
		 */
		if (SOSP_COUNTER_THRESHOLD < 1024 * 64)
			SOSP_COUNTER_THRESHOLD = SOSP_COUNTER_THRESHOLD * 2;
		//printf("SOSP_COUNTER_THRESHOLD=%lu\n",SOSP_COUNTER_THRESHOLD);
#endif
		tx_begin();
	}
}

#endif//USE_SOSP_COUNTER


/* ignore xend without xbegin */
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>


static void end_count(void)
{
#ifdef USE_TX_STATS
	printf("tx success: %d, aborts: %d, unknown: %d, capacity: %d\n"
	       "detector cnt: %d, escaped_tx_region_count: %d\n"
	       "mem hook cnt: %llu\n",
	       tx_stats.n_success, tx_stats.n_aborts, tx_stats.n_unknown_aborts, tx_stats.n_capacity_aborts,
	       tx_stats.detector_cnt, tx_stats.escaped_tx_count,
	       tx_stats.mem_hook_count);
#endif
}


/////////////////////////////////////////////////////////


/*
 * raw txbegin and txend wrapper
 */

extern "C" void TxHookTxEnd()
{
	if (_xtest())
		_xend();
}

extern "C" void TxHookTxBegin()
{
	tx_begin();
}

/*
 * force slow/fast path for current thread
 */

extern "C" void TxHookSwitchToSlowPath()
{
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	if (get_state(pv, getHashedTID()))
		tx_end();
	else
		set_state(&pv, getHashedTID());
	tlps = TL_PATH_SLOW;
#else
	if (get_state(pv, getHashedTID()))
		clear_state(&pv, getHashedTID());
#endif
}

extern "C" void TxHookSwitchToFastPath()
{
#ifndef USE_THREAD_LOCAL_PATH_SELECTOR
	if (!get_state(pv, getHashedTID()))
		set_state(&pv, getHashedTID());
#endif

	tx_begin();
}

// DY: Don't add tx_begin() or tx_end() in TxHookInit or TxHookFini
//     These hooks are for initiailing or de-initializing internal data

extern "C" void TxHookFini()
{

#ifdef USE_TX
	end_count();
#endif
	/*
	 * set pv state, if it is in slow path, so that it will not block other threads
	 */
	if (!get_state(pv, getHashedTID()))
		set_state(&pv, getHashedTID());
#ifdef USE_DUMP_TXABRT_BRANCH
	g_lock();
	if (lbr_dump_file.is_open())
		lbr_dump_file.close();
	g_unlock();
#endif
	freeThreadInfo();
}

extern "C" void TxHookInit()
{
	atexit(TxHookFini);
#ifdef USE_DUMP_TXABRT_BRANCH
	g_lock();
	if (!lbr_dump_file.is_open())
	{
		lbr_dump_file.open(LBR_DUMP_FILE_NAME, std::ofstream::out | std::ofstream::trunc | std::ofstream::binary);
	}
	g_unlock();
#endif
}

// DY: It is now safe to add tx_end() in TxHookBeforeCall as it is added
//     only before the call to a library function leading to system calls
extern "C" void TxHookBeforeCall()
{
	if (get_state(pv, getHashedTID()))
		tx_end();
	else
		set_state(&pv, getHashedTID());
	//_WAIT_SLOW_PATH_
}

// DY: It is now safe to add tx_begin() in TxHookAfterCall as it is added
//     only after the call to a library function leading to system calls
extern "C" void TxHookAfterCall()
{
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	tlps = TL_PATH_FAST;
#endif
	tx_begin();
}

__thread pthread_key_t _txhook_key = 0;
static void cb_freeThreadInfo(void* arg)
{
	//printf("exit htid:%d\n",getHashedTID());
	if (!get_state(pv, getHashedTID()))
		set_state(&pv, getHashedTID());
	freeThreadInfo();
}

// DY: It is now safe to add tx_begin() in TxHookEnter as it is added
//     only in the beginning of entry functions (e.g., main, thread_start, signal_handler, etc.)
extern "C" void TxHookEnter(unsigned FuncID)
{
#ifdef USE_TX
	//__sync_fetch_and_add(&tgi.thread_count,1);
	//printf("thread %d created, tcnt=%d\n",gettid(),tgi.thread_count);
#endif
	/*
	 * call getThreadInfo instead of allocateThreadInfo
	 * to avoid duplicate allocation
	 */
	if (_txhook_key == 0)
	{
		getThreadInfo();
		pthread_key_create((pthread_key_t*)&_txhook_key, cb_freeThreadInfo);
		pthread_setspecific(_txhook_key, &getHashedTID());
		//printf("key created for htid:%d\n",getHashedTID());
	}
	tx_begin();
}

/*
 * When entry function begins, it is started single threads
 */

extern "C" void TxHookEnter_st(unsigned FuncID)
{
	getThreadInfo();
}

// DY: It is now safe to add tx_end() in TxHookExit as it is added
//     only at the end (ret inst.) of entry functions (e.g., main, thread_start, signal_handler, etc.)
/*
 * This is no good when thread exit is not at the entry function
 * eg: worker(){ func(); }
 *     func(){ exit 0; }
 * see TxHookEnter for workaround
 */
extern "C" void TxHookExit(unsigned FuncID)
{
	if (get_state(pv, getHashedTID()))
		tx_end();
	else
		set_state(&pv, getHashedTID());
	freeThreadInfo();
}

////////////////////////////////////////////////////////////////
//
//tsan library wrapper
//
////////////////////////////////////////////////////////////////

/*
 * USE_GOOGLE_TSAN_COMPILER_RT_LIBRARY
 * FROM: LLVM COMPILER RT
 */

#include "tsan-compiler-rt/rtl/tsan_interface.h"

extern "C"
void __wrap_tsan_init()
{
	__tsan_init();
}

extern "C"
void __wrap_tsan_read1(void *addr) {
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_read1(addr);
#endif
#ifdef USE_TX_STATS_MEM
		MEM_HOOK_COUNTER_INC;
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif
#endif
}

extern "C"
void __wrap_tsan_read2(void *addr) {
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_read2(addr);
#endif
#ifdef USE_TX_STATS_MEM
		MEM_HOOK_COUNTER_INC;
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif
#endif
}

extern "C"
void __wrap_tsan_read4(void *addr) {
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_read4(addr);
#endif
#ifdef USE_TX_STATS_MEM
		MEM_HOOK_COUNTER_INC;
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif

#endif
}

extern "C"
void __wrap_tsan_read8(void *addr) {
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_read8(addr);
#endif
#ifdef USE_TX_STATS_MEM
		MEM_HOOK_COUNTER_INC;
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif

#endif
}

extern "C"
void __wrap_tsan_read16(void *addr) {
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_read16(addr);
#endif
#ifdef USE_TX_STATS_MEM
		MEM_HOOK_COUNTER_INC;
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif

#endif
}

extern "C"
void __wrap_tsan_write1(void *addr) {
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_write1(addr);
#endif
#ifdef USE_TX_STATS_MEM
		MEM_HOOK_COUNTER_INC;
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif

#endif
}

extern "C"
void __wrap_tsan_write2(void *addr) {
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_write2(addr);
#endif
#ifdef USE_TX_STATS_MEM
		MEM_HOOK_COUNTER_INC;
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif

#endif
}

extern "C"
void __wrap_tsan_write4(void *addr) {
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_write4(addr);
#endif
#ifdef USE_TX_STATS_MEM
		MEM_HOOK_COUNTER_INC;
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif

#endif
}
extern "C"
void __wrap_tsan_write8(void *addr) {
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_write8(addr);
#endif
#ifdef USE_TX_STATS_MEM
		MEM_HOOK_COUNTER_INC;
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif

#endif
}

extern "C"
void __wrap_tsan_write16(void *addr) {
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_write16(addr);
#endif
#ifdef USE_TX_STATS_MEM
		MEM_HOOK_COUNTER_INC;
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif

#endif
}

extern "C"
void __wrap_tsan_unaligned_read2(void *addr) {
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_unaligned_read2(addr);
#endif
#ifdef USE_TX_STATS_MEM
		MEM_HOOK_COUNTER_INC;
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif

#endif
}

extern "C"
void __wrap_tsan_unaligned_read4(void *addr) {
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_unaligned_read4(addr);
#endif
#ifdef USE_TX_STATS_MEM
		MEM_HOOK_COUNTER_INC;
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif

#endif
}

extern "C"
void __wrap_tsan_unaligned_read8(void *addr) {
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_unaligned_read8(addr);
#endif
#ifdef USE_TX_STATS_MEM
		MEM_HOOK_COUNTER_INC;
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif

#endif
}

extern "C"
void __wrap_tsan_unaligned_read16(void *addr) {
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_unaligned_read16(addr);
#endif
#ifdef USE_TX_STATS_MEM
		MEM_HOOK_COUNTER_INC;
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif

#endif
}


extern "C"
void __wrap_tsan_unaligned_write2(void *addr) {
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_unaligned_write2(addr);
#endif
#ifdef USE_TX_STATS_MEM
		MEM_HOOK_COUNTER_INC;
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif

#endif
}

extern "C"
void __wrap_tsan_unaligned_write4(void *addr) {
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_unaligned_write4(addr);
#endif
#ifdef USE_TX_STATS_MEM
		MEM_HOOK_COUNTER_INC;
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif

#endif
}
extern "C"
void __wrap_tsan_unaligned_write8(void *addr) {
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_unaligned_write8(addr);
#endif
#ifdef USE_TX_STATS_MEM
		MEM_HOOK_COUNTER_INC;
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif

#endif
}

extern "C"
void __wrap_tsan_unaligned_write16(void *addr) {
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_unaligned_write16(addr);
#endif
#ifdef USE_TX_STATS_MEM
		MEM_HOOK_COUNTER_INC;
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif

#endif
}

extern "C"
void __wrap_tsan_vptr_update(void **vptr_p, void *new_val)
{
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_vptr_update(vptr_p, new_val);
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif

#endif
}

extern "C"
void __wrap_tsan_vptr_read(void **vptr_p)
{
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_vptr_read(vptr_p);
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif

#endif
}

extern "C"
void __wrap_tsan_func_entry(void *pc)
{
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_func_entry(pc);
#endif
	}
#endif
}

extern "C"
void __wrap_tsan_func_exit()
{
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_func_exit();
#endif
	}
#endif
}

extern "C"
void __wrap_tsan_read_range(void *addr, uptr size)
{
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_read_range(addr, size);
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif

#endif
}

extern "C"
void __wrap_tsan_write_range(void *addr, uptr size)
{
#ifndef EMPTY_TEST
	if ((!get_state(pv, getHashedTID()))
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	        || (tlps == TL_PATH_SLOW)
#endif
	   )
	{
#ifdef USE_SAMPLING
		if ((fastrand() % 100) >= SAMPLING_RATE) return;
#endif
#ifdef USE_TSAN_CRT
		__tsan_write_range(addr, size);
#endif
	}
#ifdef USE_SOSP_COUNTER
	else sosp_logic();
#endif

#endif
}

#ifndef _USE_DYNAMIC_LOOP_CUT_THRESHOLD_

//////////////////////////////////////////////////////////////////////////////
/*
 * for tx loop cutting
 * this should be thread specific
 * -----
 * loopid is actually InsID
 * loop_counter is actually loop incremental counter
 * threshold is actually read from threashold file
 * ----------------------------------------------------------------
 * all loopid shares the same loop counter,
 * when loopid changes, reset loop counter to zero
 */

__thread struct tx_loop_cut_status
{
	int mon_loopid = -1;
	int loop_counter = 0;
} __attribute__((aligned(64))) tlcs;


extern "C"
void __tx_cut_loop(int loopid, int threshold)
{
	if (unlikely((tlcs.mon_loopid != loopid)))
	{
		tlcs.mon_loopid = loopid;
		tlcs.loop_counter = 0;
	}
	tlcs.loop_counter++;
	if (unlikely((tlcs.loop_counter >= threshold)))
	{
		TxHookBeforeCall();
		//printf("tx_cut_loop:%d,%d\n", loopid, threshold);
		tlcs.loop_counter = 0;
		TxHookAfterCall();
	}
}

#else//_USE_DYNAMIC_LOOP_CUT_THRESHOLD_
/*
 * Dynamic Loop Threshold Adjustment
 * increase from 0 to the value which cause abort
 */

#define _LOOP_TRACK_MAX_ (1<<3)
#define _LOOP_TRACK_MAX_MASK_ (_LOOP_TRACK_MAX_-1)

__thread struct tx_loop_cut_status
{
	int mon_loopid = -1;
	int loop_counter = 0;
	int loop_threshold = 0;
	int abrt_counter = 0;
} __attribute__((aligned(64))) tlcs[_LOOP_TRACK_MAX_];

inline int hash_loopid(int loopid)
{
	return ((loopid >> 4) & (loopid)) & _LOOP_TRACK_MAX_MASK_;
}

tx_loop_cut_status* getLoopState(int loopid)
{
	int hashedLoopId = hash_loopid(loopid);
	do
	{
		if (tlcs[hashedLoopId].mon_loopid == loopid)
		{
			break;
		}
		else if (tlcs[hashedLoopId].mon_loopid == -1)
		{
			tlcs[hashedLoopId].mon_loopid = loopid;
			break;
		}
		hashedLoopId = (hashedLoopId + 1) % _LOOP_TRACK_MAX_MASK_;
	} while (1);
	return &tlcs[hashedLoopId];
}

#ifdef _USE_LINEAR_THRESHOLD_ADJUSTMENT_

extern "C"
void __tx_cut_loop(int loopid, int threshold)
{
	tx_loop_cut_status* ptlcs = getLoopState(loopid);
	ptlcs->loop_counter++;
	if (ptlcs->loop_counter < ptlcs->loop_threshold)
	{
		return;
	}
	//end tx
	if (_xtest())
	{
		_xend();
		//increase threshold
		ptlcs->loop_threshold++;
	}
	//reset counter
	ptlcs->loop_counter = 0;


	//start tx
	// most of this part is copied from tx_begin()
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	/*
	 * reset thread local path to fast path
	 */
	tlps = TL_PATH_FAST;
#endif

#ifdef _USE_SAFE_SYNC_
	int toc = 0xFFFFF;
	while ((pv & PV_MASK_FAST_PATH) != PV_FAST_PATH)
	{
		if (toc-- == 0)
		{
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
#ifdef USE_TL_SLOW_PATH_WHEN_TIMEOUT
			tlps = TL_PATH_SLOW;
			return;
#endif
#endif
			break;//go on
		}

	}
#else
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	if ((pv & PV_MASK_FAST_PATH) != PV_FAST_PATH)
	{
		tlps = TL_PATH_SLOW;
		return;
	}
#endif
#endif
	unsigned int status;
	////////////////////////////////////////////////////////////////////////
	if ((status = _xbegin()) == _XBEGIN_STARTED)
	{
		if (unlikely((pv & PV_MASK_FAST_PATH) != PV_FAST_PATH))
		{
			_xabort(0x00);
		}
	} else {
		//decrease threshold
		if (likely(ptlcs->loop_threshold > 0))
			ptlcs->loop_threshold--;

		switch (status)
		{
		case (0):

#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
#ifdef USE_TL_SLOW_PATH_WHEN_UNKNOWN_ABRT
			/*
			 * Unknown abort, set current thread to slow path
			 */
			tlps = TL_PATH_SLOW;
#endif
#endif
			break;
		/*
		 * Capacity abort
		 */
		case (_XABORT_CAPACITY):
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
#ifdef USE_TL_SLOW_PATH_WHEN_CAP_ABRT
			/*
			 * Capacity abort, set current thread to slow path
			 */
			tlps = TL_PATH_SLOW;
#endif
#endif
			break;
		/*
		 * Explicitly abort
		 */
		case (_XABORT_EXPLICIT):
			/*
			 * SLOWPATH
			 * should also go for slow path, although this thread may be the winner thread
			 * and in this situation, all tx-slow-path should be avoided
			 * And this always comes after XABORT_CONFLICT ( for global_abort_variable )
			 */
			break;

		default:
#ifdef USE_TX_STATS
			if ((pv & PV_MASK_FAST_PATH) == PV_MASK_FAST_PATH)
			{
				tx_stats.detector_cnt++;
			}
#endif
			clear_state(&pv, getHashedTID()); //set to TXMODE_FASTTRACK)
			break;
		}
	}
}
//_USE_LINEAR_THRESHOLD_ADJUSTMENT_
#elif defined(_USE_EXPONENTIAL_THRESHOLD_ADJUSTMENT_)
extern "C"
void __tx_cut_loop(int loopid, int threshold)
{
	tx_loop_cut_status* ptlcs = getLoopState(loopid);
	ptlcs->loop_counter++;
	if (ptlcs->loop_counter < ptlcs->loop_threshold)
	{
		return;
	}
	//end tx
	if (_xtest())
	{
		_xend();
		//increase threshold
		if (ptlcs->loop_threshold == 0)
			ptlcs->loop_threshold = 1;
		else
			ptlcs->loop_threshold  = ptlcs->loop_threshold << 1;
		ptlcs->abrt_counter = 0;
	}
	//reset counter
	ptlcs->loop_counter = 0;

	//start tx
	// most of this part is copied from tx_begin()
retry:
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	/*
	 * reset thread local path to fast path
	 */
	tlps = TL_PATH_FAST;
#endif

#ifdef _USE_SAFE_SYNC_
	int toc = 0xFFFFF;
	while ((pv & PV_MASK_FAST_PATH) != PV_FAST_PATH)
	{
		if (toc-- == 0)
		{
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
#ifdef USE_TL_SLOW_PATH_WHEN_TIMEOUT
			tlps = TL_PATH_SLOW;
			return;
#endif
#endif
			break;//go on
		}

	}
#else
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
	if ((pv & PV_MASK_FAST_PATH) != PV_FAST_PATH)
	{
		tlps = TL_PATH_SLOW;
		return;
	}
#endif
#endif
	unsigned int status;
	////////////////////////////////////////////////////////////////////////
	if ((status = _xbegin()) == _XBEGIN_STARTED)
	{
		if (unlikely((pv & PV_MASK_FAST_PATH) != PV_FAST_PATH))
		{
			_xabort(0x00);
		}
	} else {
		//decrease threshold
		if (likely(ptlcs->loop_threshold > 0))
		{
			if (ptlcs->loop_threshold == 1)
				ptlcs->loop_threshold = 0;
			else
				ptlcs->loop_threshold = ptlcs->loop_threshold >> 1;
		}

		if (ptlcs->abrt_counter++ < 3)
			goto retry;
		ptlcs->loop_threshold = 0;
		ptlcs->abrt_counter = 0;
		switch (status)
		{
		case (0):

#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
#ifdef USE_TL_SLOW_PATH_WHEN_UNKNOWN_ABRT
			/*
			 * Unknown abort, set current thread to slow path
			 */
			tlps = TL_PATH_SLOW;
#endif
#endif
			break;
		/*
		 * Capacity abort
		 */
		case (_XABORT_CAPACITY):
#ifdef USE_THREAD_LOCAL_PATH_SELECTOR
#ifdef USE_TL_SLOW_PATH_WHEN_CAP_ABRT
			/*
			 * Capacity abort, set current thread to slow path
			 */
			tlps = TL_PATH_SLOW;
#endif
#endif
			break;
		/*
		 * Explicitly abort
		 */
		case (_XABORT_EXPLICIT):
			/*
			 * SLOWPATH
			 * should also go for slow path, although this thread may be the winner thread
			 * and in this situation, all tx-slow-path should be avoided
			 * And this always comes after XABORT_CONFLICT ( for global_abort_variable )
			 */
			break;

		default:
#ifdef USE_TX_STATS
			if ((pv & PV_MASK_FAST_PATH) == PV_MASK_FAST_PATH)
			{
				tx_stats.detector_cnt++;
			}
#endif
			clear_state(&pv, getHashedTID()); //set to TXMODE_FASTTRACK)
			break;
		}
	}
}
#endif

#endif//_USE_DYNAMIC_LOOP_CUT_THRESHOLD_

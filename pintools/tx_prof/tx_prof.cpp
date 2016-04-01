/*
 * sourcode base was cloned from Dr. Lee.
 * Jun 2015 Tong Zhang<ztong@vt.edu>
 */

#include <iostream>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <set>
#include <map>
#include "pin.H"


//#define _DEBUG


KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
                            "o", "tx_prof.out", "specify output file name");

KNOB<BOOL> KnobTxSim(KNOB_MODE_WRITEONCE, "pintool",
                     "txsim", "0", "simulate tx");

PIN_LOCK lock;
INT32 numThreads = 0;

INT32 threadCounter = 0;

// Force each thread's data to be in its own data cache line so that
// multiple threads do not contend for the same data cache line.
// This avoids the false sharing problem.
//#define PADSIZE 64-8  // 64 byte line size: 64-4*2

// thread local data structure
class thread_data_t
{
public:
    thread_data_t() :
        _before_call(0),
        _syscall(0),
        _tx_id(0),
        _tx_region(0),
        _in_single_t(0)
    {}
    UINT32 _before_call;
    UINT32 _syscall;
    UINT32 _tx_id;
    UINT32 _tx_region;
    UINT32 _in_single_t;
    UINT32 _ins_tx_last_entry_st;
    std::map<UINT32/*tx_id*/, std::set<void *>/*addrs*/> _cache_blocks;
    //A list of tx func id where there is only 1(main) thread running, thread local
    std::set<UINT32> t_useless_b_txfuncid;//before
    std::set<UINT32> t_useless_a_txfuncid;//after
    //A list of tx func id where there is more than 1 thread running, thread local
    std::set<UINT32> t_useful_b_txfuncid;//before
    std::set<UINT32> t_useful_a_txfuncid;//after

    //UINT8 _pad[PADSIZE];
};

// a list of entry function id, and libsys call instruction id
// NOTE: read only for TxSim (no race), after initilized in the beginning
std::set<UINT32> g_entry_funcid;
std::set<UINT32> g_libsys_insid;

/*
 * Entry point with single thread
 */
std::set<UINT32> g_entry_st_funcid;

/*
 * A list of tx func id where there is only 1(main) thread running
 */
std::set<UINT32> g_useless_b_txfuncid;//before
std::set<UINT32> g_useless_a_txfuncid;//after

/*
 * A list of tx func id where there is more than 1 thread running
 */

std::set<UINT32> g_useful_b_txfuncid;//before
std::set<UINT32> g_useful_a_txfuncid;//after

// a list of function ids executed before TxProfInit or after TxProfFini
std::set<UINT32> g_blacklist_funcid;
static bool g_between_init_and_fini;

// key for accessing TLS storage in the threads. initialized once in main()
static  TLS_KEY tls_key;

// function to access thread-specific data
thread_data_t* get_tls(THREADID threadid)
{
    thread_data_t* tdata =
        static_cast<thread_data_t*>(PIN_GetThreadData(tls_key, threadid));
    return tdata;
}

void parse_log_file(FILE *file)
{
    char line[256] = {0,};
    //parse and process
    while (fgets( line, sizeof(line) - 1, file) != NULL)
    {
        int id = 0;
        if (sscanf(line, "X:%d", &id) == 1) {
            g_blacklist_funcid.insert(id);

            //std::cout << "entry_funcid=" << id << endl << flush;

        }
        else if (sscanf(line, "E:%d", &id) == 1) {
            g_entry_funcid.insert(id);

            //std::cout << "entry_funcid=" << id << endl << flush;

        }
        else if (sscanf(line, "ES:%d", &id) == 1) {
            g_entry_st_funcid.insert(id);
        }
        else if (sscanf(line, "S:%d", &id) == 1) {
            g_libsys_insid.insert(id);

            //std::cout << "libsys_insid=" << id << endl << flush;

        }
        else if (sscanf(line, "OA:%d", &id) == 1)
        {
            g_useless_a_txfuncid.insert(id);
        }
        else if (sscanf(line, "OB:%d", &id) == 1)
        {
            g_useless_b_txfuncid.insert(id);
        }
        else if (sscanf(line, "UA:%d", &id) == 1)
        {
            g_useful_a_txfuncid.insert(id);
        }
        else if (sscanf(line, "UB:%d", &id) == 1)
        {
            g_useful_b_txfuncid.insert(id);
        }
        else {
            std::cerr << "[ERROR] could not parse: " << line << endl;
            exit(0);
        }
    }

}

VOID InitLogFile() {

    if (KnobTxSim) {

        // update g_entry_funcid and g_libsys_insid
        FILE * file = fopen(KnobOutputFile.Value().c_str(), "r");
        if (!file) {
            std::cerr << "[ERROR] could not open log file " << KnobOutputFile.Value().c_str() << endl;
            exit(0);
        }
        parse_log_file(file);
        fclose(file);

    } else {

        // if previous log exists, then update g_entry_funcid and g_libsys_insid
        // to accumulate the results of multiple profile runs
        FILE * file = fopen(KnobOutputFile.Value().c_str(), "r");
        if (file) {
            parse_log_file(file);
            fclose(file);
        }

    }

}

VOID ThreadStart(THREADID threadid, CONTEXT *ctxt, INT32 flags, VOID *v)
{
    PIN_GetLock(&lock, threadid + 1);
    numThreads++;
    threadCounter++;
    PIN_ReleaseLock(&lock);

    std::cout << "threadCounter+: " << threadCounter << std::endl;

    thread_data_t* tdata = new thread_data_t;

    PIN_SetThreadData(tls_key, tdata, threadid);
}

VOID ThreadFini(THREADID threadid, const CONTEXT *ctxt, INT32 flags, VOID *v)
{
    thread_data_t* tdata = get_tls(threadid);
    PIN_GetLock(&lock, threadid + 1);
    /*
     * merge data in thread local container into global container before exit
     */
    for (std::set<UINT32>::iterator it=tdata->t_useful_b_txfuncid.begin();
            it!=tdata->t_useful_b_txfuncid.end();
            ++it)
    {
        g_useful_b_txfuncid.insert(*it);
    }
    for (std::set<UINT32>::iterator it=tdata->t_useful_a_txfuncid.begin();
            it!=tdata->t_useful_a_txfuncid.end();
            ++it)
    {
        g_useful_a_txfuncid.insert(*it);
    }
    for (std::set<UINT32>::iterator it=tdata->t_useless_b_txfuncid.begin();
            it!=tdata->t_useless_b_txfuncid.end();
            ++it)
    {
        g_useless_b_txfuncid.insert(*it);
    }
    for (std::set<UINT32>::iterator it=tdata->t_useless_a_txfuncid.begin();
            it!=tdata->t_useless_a_txfuncid.end();
            ++it)
    {
        g_useless_a_txfuncid.insert(*it);
    }
    threadCounter--;
    PIN_ReleaseLock(&lock);
    std::cout << "threadCounter-: " << threadCounter << std::endl;
}

VOID __TxBegin(THREADID threadid, UINT32 id) /* funcid for entry, insid for the rest (common) */
{
    thread_data_t* tdata = get_tls(threadid);

    if (tdata->_tx_region) {
        std::cerr << "[ERROR] __TxBegin(tx_id:" << tdata->_tx_id << ") and another __TxBegin ??" << endl;
        exit(0);
    }

    tdata->_tx_region = 1;

#ifdef _DEBUG
    for (THREADID i = 0; i < threadid; i++) std::cout << "\t\t";
    std::cout << "[" << threadid << "]__TxBegin(tx:" << tdata->_tx_id << ",id:" << id << ")" << endl << flush;
#endif
}

VOID __TxEnd(THREADID threadid, UINT32 id) /* funcid for exit, insid for the rest (common) */
{
    thread_data_t* tdata = get_tls(threadid);

    if (tdata->_tx_region == 0) {
        std::cerr << "[ERROR] __TxEnd(tx_id:" << tdata->_tx_id << ") w/o __TxBegin ??" << endl;
        exit(0);
    }

    tdata->_tx_region = 0;

#ifdef _DEBUG
    for (THREADID i = 0; i < threadid; i++) std::cout << "\t\t";
    std::cout << "[" << threadid << "]__TxEnd(tx:" << tdata->_tx_id << ",id:" << id << ")" << endl << flush;
#endif

    tdata->_tx_id++;
}

VOID TxProfInit(THREADID threadid)
{
#ifdef _DEBUG
    std::cout << "[" << threadid << "]TxProfInit" << endl << flush;
#endif
    g_between_init_and_fini = true;
}

VOID TxProfFini(THREADID threadid)
{
    g_between_init_and_fini = false;
#ifdef _DEBUG
    std::cout << "[" << threadid << "]TxProfFini" << endl << flush;
#endif
}

VOID TxProfBeforeCall(THREADID threadid, UINT32 insid)
{
#ifdef _DEBUG
    std::cout << "[" << threadid << "]TxProfBeforeCall(" << insid << ")" << endl << flush;
#endif

    thread_data_t* tdata = get_tls(threadid);

    if (KnobTxSim) {

        std::set<UINT32>::iterator it = g_libsys_insid.find(insid);
        if (it != g_libsys_insid.end()) {
            __TxEnd(threadid, insid);
        }

        tdata->_before_call = insid; //to give hint when syscall is detected in TX
    }
    else {
        tdata->_before_call = 1;
        tdata->_syscall = 0;    // reset, so that we can detect syscall after beforecall

        /*
         * if thread count<2, ST, otherwise MT
         * store info in thread local data container
         * merge data info global data container when thread finishes its execution
         */
        //PIN_GetLock(&lock, threadid + 1);
        if (threadCounter < 2)
        {
            bool notexists = (tdata->t_useful_b_txfuncid.find(insid) == tdata->t_useful_b_txfuncid.end());
            if (notexists)
            {
                tdata->t_useless_b_txfuncid.insert(insid);
            }
        } else
        {
            tdata->t_useful_b_txfuncid.insert(insid);
        }
        //PIN_ReleaseLock(&lock);
        //reset
        tdata->_in_single_t = 0;
    }
}

VOID TxProfAfterCall(THREADID threadid, UINT32 insid)
{
#ifdef _DEBUG
    std::cout << "[" << threadid << "]TxProfAfterCall(" << insid << ")" << endl << flush;
#endif

    thread_data_t* tdata = get_tls(threadid);

    if (KnobTxSim) {

        std::set<UINT32>::iterator it = g_libsys_insid.find(insid);
        if (it != g_libsys_insid.end()) {
            __TxBegin(threadid, insid);
        }

    }
    else {
        if (tdata->_before_call) { // we don't have a callee --> it was a library call
            //check syscall
            if (tdata->_syscall) {
#ifdef _DEBUG
                std::cout << "[" << threadid << "]TxProfAfterCall(" << insid << ") -- SYSCALL" << endl << flush;
#endif
                PIN_GetLock(&lock, threadid + 1);
                g_libsys_insid.insert(insid);
                PIN_ReleaseLock(&lock);
            }
            tdata->_before_call = 0;    // reset
        }
        /*
         * if thread count<2, ST, otherwise MT
         */
        //PIN_GetLock(&lock, threadid + 1);
        if (threadCounter < 2)
        {
            bool notexists = (tdata->t_useful_a_txfuncid.find(insid) == tdata->t_useful_a_txfuncid.end());
            if (notexists)
            {
                tdata->t_useless_a_txfuncid.insert(insid);
            }
        } else
        {
            tdata->t_useful_a_txfuncid.insert(insid);
        }
        //PIN_ReleaseLock(&lock);
    }
}

VOID TxProfEnter(THREADID threadid, UINT32 funcid)
{
#ifdef _DEBUG
    std::cout << "[" << threadid << "]TxProfEnter(" << funcid << ")" << endl << flush;
#endif

    thread_data_t* tdata = get_tls(threadid);

    if (KnobTxSim) {

        std::set<UINT32>::iterator it = g_entry_funcid.find(funcid);
        if (it != g_entry_funcid.end()) {
            __TxBegin(threadid, funcid);
        }

    }
    else {
        if (tdata->_before_call) { // we have both caller and callee
            tdata->_before_call = 0;    // reset
        } else { // this should be entry point (main or thread_init)
#ifdef _DEBUG
            std::cout << "[" << threadid << "]TxProfEnter(" << funcid << ") -- ENTRY" << endl << flush;
#endif
            PIN_GetLock(&lock, threadid + 1);
            g_entry_funcid.insert(funcid);
            if (threadCounter < 2)
            {
                g_entry_st_funcid.insert(funcid);
                tdata->_in_single_t = 1;
            }
            PIN_ReleaseLock(&lock);
        }

        // we are not interested in functions not in between init and fini
        if (!g_between_init_and_fini) {
            PIN_GetLock(&lock, threadid + 1);
            g_blacklist_funcid.insert(funcid);
            PIN_ReleaseLock(&lock);
        }
    }
}

VOID TxProfExit(THREADID threadid, UINT32 funcid)
{
#ifdef _DEBUG
    std::cout << "[" << threadid << "]TxProfExit(" << funcid << ")" << endl << flush;
#endif

    if (KnobTxSim) {

        std::set<UINT32>::iterator it = g_entry_funcid.find(funcid);
        if (it != g_entry_funcid.end()) {
            __TxEnd(threadid, funcid);
        }

    }
    else {

        //nop

    }
}

VOID TxProfSyscall(THREADID threadid, ADDRINT sysnum)
{
#ifdef _DEBUG
    std::cout << "[" << threadid << "]syscall(" << sysnum << ")" << endl << flush;
#endif

    thread_data_t* tdata = get_tls(threadid);

    if (KnobTxSim) {

        if (tdata->_tx_region) {
            std::cout << "[" << threadid << "]syscall(" << sysnum << ") -- [ERROR] syscall in the TX("
                      << tdata->_tx_id << ") MAYBE add S:" << tdata->_before_call << endl << flush;
            exit(0);
        }

    }
    else {

        tdata->_syscall = 1;

    }
}

/*
VOID TxProfAtomic(THREADID threadid, ADDRINT ip)
{
#ifdef _DEBUG
    std::cout << "[" << threadid << "]atomic(" << ip << ")" << endl << flush;
#endif
}
*/

// a memory read
VOID TxProfMemRead(THREADID threadid, ADDRINT ip, ADDRINT addr)
{
    //fprintf(trace,"%p: R %p\n", ip, addr);

    thread_data_t* tdata = get_tls(threadid);

    if (tdata->_tx_region) {

        ADDRINT cache_block_addr;
        PIN_SafeCopy(&cache_block_addr, (VOID *)addr, sizeof(ADDRINT));
        cache_block_addr = cache_block_addr >> 6;

        tdata->_cache_blocks[tdata->_tx_id].insert((VOID *)cache_block_addr);
    }
}

// a memory write
VOID TxProfMemWrite(THREADID threadid, ADDRINT ip, ADDRINT addr)
{
    //fprintf(trace,"%p: W %p\n", ip, addr);

    thread_data_t* tdata = get_tls(threadid);

    if (tdata->_tx_region) {

        ADDRINT cache_block_addr;
        PIN_SafeCopy(&cache_block_addr, (VOID *)addr, sizeof(ADDRINT));
        cache_block_addr = cache_block_addr >> 6;

        tdata->_cache_blocks[tdata->_tx_id].insert((VOID *)cache_block_addr);
    }
}

/*

// This function is called before every block
VOID PIN_FAST_ANALYSIS_CALL docount(UINT32 c, THREADID threadid)
{
    thread_data_t* tdata = get_tls(threadid);
    tdata->_count += c;
}

// Pin calls this function every time a new basic block is encountered.
// It inserts a call to docount.
VOID Trace(TRACE trace, VOID *v)
{
    // Visit every basic block  in the trace
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        // Insert a call to docount for every bbl, passing the number of instructions.

        BBL_InsertCall(bbl, IPOINT_ANYWHERE, (AFUNPTR)docount, IARG_FAST_ANALYSIS_CALL,
                       IARG_UINT32, BBL_NumIns(bbl), IARG_THREAD_ID, IARG_END);
    }
}
*/

VOID Routine(RTN rtn, VOID *v)
{
    RTN_Open(rtn);

    string rtn_name = RTN_Name(rtn);

    if (rtn_name == "TxProfInit") {
        RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)TxProfInit, IARG_THREAD_ID, IARG_END);
    }
    else if (rtn_name == "TxProfFini") {
        RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)TxProfFini, IARG_THREAD_ID, IARG_END);
    }
    else if (rtn_name == "TxProfBeforeCall") {
        RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)TxProfBeforeCall,
                       IARG_THREAD_ID,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                       IARG_END);
    }
    else if (rtn_name == "TxProfAfterCall") {
        RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)TxProfAfterCall,
                       IARG_THREAD_ID,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                       IARG_END);
    }
    else if (rtn_name == "TxProfEnter") {
        RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)TxProfEnter,
                       IARG_THREAD_ID,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                       IARG_END);
    }
    else if (rtn_name == "TxProfExit") {
        RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)TxProfExit,
                       IARG_THREAD_ID,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                       IARG_END);
    }

    // For each instruction of the routine
    for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins))
    {
        // To instrument system calls
        if (INS_IsSyscall(ins)) {
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)TxProfSyscall,
                           IARG_THREAD_ID,
                           IARG_SYSCALL_NUMBER,
                           IARG_END);
        }

        /*
        // To instrument atomic operations
        if(INS_IsAtomicUpdate(ins)){
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)TxProfAtomic,
                    IARG_THREAD_ID,
                    IARG_INST_PTR,
                    IARG_END);
        }
        */

        // Instruments memory accesses using a predicated call, i.e.
        // the instrumentation is called iff the instruction will actually be executed.
        //
        // On the IA-32 and Intel(R) 64 architectures conditional moves and REP
        // prefixed instructions appear as predicated instructions in Pin.
        UINT32 memOperands = INS_MemoryOperandCount(ins);

        // Iterate over each memory operand of the instruction.
        for (UINT32 memOp = 0; memOp < memOperands; memOp++)
        {
            if (INS_MemoryOperandIsRead(ins, memOp))
            {
                INS_InsertPredicatedCall(
                    ins, IPOINT_BEFORE, (AFUNPTR)TxProfMemRead,
                    IARG_THREAD_ID,
                    IARG_INST_PTR,
                    IARG_MEMORYOP_EA, memOp,
                    IARG_END);
            }
            // Note that in some architectures a single memory operand can be
            // both read and written (for instance incl (%eax) on IA-32)
            // In that case we instrument it once for read and once for write.
            if (INS_MemoryOperandIsWritten(ins, memOp))
            {
                INS_InsertPredicatedCall(
                    ins, IPOINT_BEFORE, (AFUNPTR)TxProfMemWrite,
                    IARG_THREAD_ID,
                    IARG_INST_PTR,
                    IARG_MEMORYOP_EA, memOp,
                    IARG_END);
            }
        }
    }

    RTN_Close(rtn);
}

// This function is called when the application exits
VOID Fini(INT32 code, VOID *v)
{

    if (KnobTxSim) {

        std::cout << endl;
        std::cout << "========== TX_PROF PINTOOL (transaction memory footprint analysis) =======" << endl;

        for (INT32 tid = 0; tid < numThreads; tid++)
        {
            thread_data_t* tdata = get_tls(tid);
            std::map<UINT32/*tx_id*/, std::set<void *>/*addrs*/>::iterator it;
            for (it = tdata->_cache_blocks.begin(); it != tdata->_cache_blocks.end(); ++it)
            {
                UINT32 tx_id = it->first;
                std::set<void *> addrs = it->second;
                std::cout << "T[" << tid  << "]-TX[" << tx_id << "] # of unique cache blocks:" << addrs.size() << endl;
            }
        }

        std::cout << "==========================================================================" << endl;
        std::cout << endl;

    } else
    {
        // write-back
        FILE * file;
        file = fopen(KnobOutputFile.Value().c_str(), "w");
        if (!file)
        {
            std::cerr << "[ERROR] could not open log file: " << KnobOutputFile.Value().c_str() << endl;
            exit(0);
        }

        std::set<UINT32>::iterator it;

        for (it = g_blacklist_funcid.begin(); it != g_blacklist_funcid.end(); ++it)
        {
            UINT32 funcid = *it;
            fprintf(file, "X:%d\n", funcid);
        }

        for (it = g_entry_funcid.begin(); it != g_entry_funcid.end(); ++it)
        {
            UINT32 funcid = *it;
            fprintf(file, "E:%d\n", funcid);
        }

        for (it = g_entry_st_funcid.begin(); it != g_entry_st_funcid.end(); ++it)
        {
            UINT32 funcid = *it;
            fprintf(file, "ES:%d\n", funcid);
        }

        for (it = g_libsys_insid.begin(); it != g_libsys_insid.end(); ++it)
        {
            UINT32 insid = *it;
            fprintf(file, "S:%d\n", insid);
        }

        for (it = g_useless_a_txfuncid.begin(); it != g_useless_a_txfuncid.end(); ++it)
        {
            UINT32 insid = *it;
            fprintf(file, "OA:%d\n", insid);
        }
        for (it = g_useless_b_txfuncid.begin(); it != g_useless_b_txfuncid.end(); ++it)
        {
            UINT32 insid = *it;
            fprintf(file, "OB:%d\n", insid);
        }

        for (it = g_useful_a_txfuncid.begin(); it != g_useful_a_txfuncid.end(); ++it)
        {
            UINT32 insid = *it;
            fprintf(file, "UA:%d\n", insid);
        }
        for (it = g_useful_b_txfuncid.begin(); it != g_useful_b_txfuncid.end(); ++it)
        {
            UINT32 insid = *it;
            fprintf(file, "UB:%d\n", insid);
        }
        fclose(file);

        std::cout << endl;
        std::cout << "========== TX_PROF PINTOOL (libsys function profiling) ===================" << endl;
        std::cout << "Blist FuncID #: " << g_blacklist_funcid.size() << endl;
        std::cout << "Entry FuncID #: " << g_entry_funcid.size() << endl;
        std::cout << "Entry ST FuncID #: " << g_entry_st_funcid.size() << endl;
        std::cout << "Libsys InsID #: " << g_libsys_insid.size() << endl;
        std::cout << "Single Thread Txins:" << g_useless_a_txfuncid.size() + g_useless_b_txfuncid.size() << endl;
        std::cout << "MT Txins:" << g_useful_a_txfuncid.size() + g_useful_b_txfuncid.size() << endl;
        std::cout << "==========================================================================" << endl;
        std::cout << endl;
    }
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    cerr << "Tx Profile" << endl;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char * argv[])
{
    // Initialize pin
    PIN_InitSymbols();
    if (PIN_Init(argc, argv)) return Usage();

    InitLogFile();

    // Initialize the lock
    PIN_InitLock(&lock);

    // Obtain  a key for TLS storage.
    tls_key = PIN_CreateThreadDataKey(0);

    // Register ThreadStart to be called when a thread starts.
    PIN_AddThreadStartFunction(ThreadStart, 0);

    // Register ThreadFini to be called when a thread exits.
    PIN_AddThreadFiniFunction(ThreadFini, 0);

    // Register Routine to be called to instrument rtn
    RTN_AddInstrumentFunction(Routine, 0);

    // Register Fini to be called when the application exits.
    PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}

/*
 * do nothing run time library
 * for __tsan_* function when compiling tx binary
 *
 * Jun 2015 by Tong Zhang <ztong@vt.edu>
 */

typedef unsigned long uptr;

extern "C"
{
	void __tsan_init(){};
	void __tsan_read1(void*addr){};
	void __tsan_read2(void*addr){};
	void __tsan_read4(void*addr){};
	void __tsan_read8(void*addr){};
	void __tsan_read16(void*addr){};

	void __tsan_write1(void*addr){};
	void __tsan_write2(void*addr){};
	void __tsan_write4(void*addr){};
	void __tsan_write8(void*addr){};
	void __tsan_write16(void*addr){};

	void __tsan_unaligned_read2(void*addr){};
	void __tsan_unaligned_read4(void*addr){};
	void __tsan_unaligned_read8(void*addr){};
	void __tsan_unaligned_read16(void*addr){};


	void __tsan_unaligned_write2(void*addr){};
	void __tsan_unaligned_write4(void*addr){};
	void __tsan_unaligned_write8(void*addr){};
	void __tsan_unaligned_write16(void*addr){};


	void __tsan_vptr_update(void **vptr_p, void *new_val){};
	void __tsan_vptr_read(void **vptr_p){};

	void __tsan_func_entry(void *pc){};
	void __tsan_func_exit(){};
	void __tsan_read_range(void *addr, uptr size){};
	void __tsan_write_range(void *addr, uptr size){};

};


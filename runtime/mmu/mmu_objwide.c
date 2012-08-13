#include "klee/klee.h"
#include "mmu_testptr.h"
#include "mmu.h"

#define PAGE_SZ		0x1000
#define PAGE_MASK	~(0xfffULL)

#define MMU_LOAD(x,y)		\
y mmu_load_##x##_objwide(void* addr) {	\
	uint64_t	p_64 = (uint64_t)addr, c_64;		\
	mmu_testptr(addr);			\
	c_64 = klee_fork_all(p_64 & PAGE_MASK);		\
	if (p_64 + sizeof(y) <= (c_64 + PAGE_SZ))	\
		return klee_wide_load_##x(		\
			(void*)klee_get_value(p_64), addr);	\
	/* misaligned */		\
	c_64 = klee_fork_all(p_64);	\
	return *((y*)c_64);		\
}

#define MMU_STORE(x,y)			\
void mmu_store_##x##_objwide(void* addr, y v) {	\
	uint64_t	p_64 = (uint64_t)addr, c_64;		\
	mmu_testptr(addr);			\
	c_64 = klee_fork_all(p_64 & PAGE_MASK);		\
	if (p_64 + sizeof(y) <= (c_64 + PAGE_SZ))		\
		klee_wide_store_##x((void*)klee_get_value(p_64), addr, v);	\
	else {	\
		/* misaligned */	\
		c_64 = klee_fork_all(p_64);	\
		*((y*)c_64) = v;	\
	}	\
}

#define MMU_ACCESS(x,y)	\
	extern y klee_wide_load_##x(void*, y*);		\
	extern void klee_wide_store_##x(void*, void*, y v);	\
	MMU_LOAD(x,y)	\
	MMU_STORE(x,y)

MMU_ACCESS(8, uint8_t)
MMU_ACCESS(16, uint16_t)
MMU_ACCESS(32, uint32_t)
MMU_ACCESS(64, uint64_t)
MMU_ACCESS(128, __uint128_t)
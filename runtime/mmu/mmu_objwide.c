#include "klee/klee.h"
#include "mmu_testptr.h"
#include "mmu.h"

#define MMU_LOAD(x,y)		\
y mmu_load_##x##_objwide(void* addr) {	\
	uint64_t	p_64 = (uint64_t)addr, c_64;		\
	mmu_testptr(addr);			\
	c_64 = klee_fork_all(p_64 & ~0xfffULL);		\
	return klee_wide_load_##x((void*)c_64, addr); }


#define MMU_STORE(x,y)			\
void mmu_store_##x##_objwide(void* addr, y v) {	\
	uint64_t	p_64 = (uint64_t)addr, c_64;		\
	mmu_testptr(addr);			\
	c_64 = klee_fork_all(p_64 & ~0xfffULL);		\
	klee_wide_store_##x((void*)c_64, addr, v); }

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
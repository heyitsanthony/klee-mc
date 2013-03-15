#include "klee/klee.h"
#include "mmu.h"

#define MMU_LOAD(x,y)		\
y mmu_load_##x##_cnulltlb(void* addr)	\
{	y *p = (y*)addr, v;		\
	v = *p;				\
	klee_tlb_insert(p, 4096);	\
	klee_enable_softmmu();		\
	return v; }

#define MMU_STORE(x,y)			\
void mmu_store_##x##_cnulltlb(void* addr, y v)	\
{	y *p = addr;	\
	*p = v;		\
	klee_tlb_insert(p, 4096);	\
	klee_enable_softmmu();	}

#define MMU_ACCESS(x,y)	\
	MMU_LOAD(x,y)	\
	MMU_STORE(x,y)

MMU_ACCESS(8, uint8_t)
MMU_ACCESS(16, uint16_t)
MMU_ACCESS(32, uint32_t)
MMU_ACCESS(64, uint64_t)
MMU_ACCESS(128, __uint128_t)
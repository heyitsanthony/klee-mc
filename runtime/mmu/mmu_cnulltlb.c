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

MMU_ACCESS_ALL()
DECL_MMUOPS_S(cnulltlb);
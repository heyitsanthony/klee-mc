#include "klee/klee.h"
#include "mmu_testptr.h"
#include "mmu.h"

MMUOPS_S_EXTERN(rangechk);

#define MMU_LOAD(x,y)			\
y mmu_load_##x##_rangechk(void* addr)	\
{	mmu_testptr(addr);		\
	return MMUOPS_S(rangechk).mo_next->mo_load_##x(addr); }

#define MMU_STORE(x,y)			\
void mmu_store_##x##_rangechk(void* addr, y v)	\
{	mmu_testptr(addr);			\
	MMUOPS_S(rangechk).mo_next->mo_store_##x(addr, v); }

MMU_ACCESS_ALL();

DECL_MMUOPS_S(rangechk);
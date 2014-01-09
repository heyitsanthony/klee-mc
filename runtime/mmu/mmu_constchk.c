#include "klee/klee.h"
#include "mmu_testptr.h"
#include "mmu.h"

MMUOPS_S_EXTERN(constchk);

static void* try_const_addr(void* addr)
{
	uint64_t	c64 = klee_get_value((uint64_t)addr);
	if (!klee_valid_eq(c64, (uint64_t)addr))
		return addr;
	klee_assume_eq(c64, addr);
	return (void*)c64;
}

#define MMU_LOAD(x,y)			\
y mmu_load_##x##_constchk(void* addr)	\
{ return MMUOPS_S(constchk).mo_next->mo_load_##x(	\
	try_const_addr(addr)); }

#define MMU_STORE(x,y)					\
void mmu_store_##x##_constchk(void* addr, y v)		\
{ MMUOPS_S(constchk).mo_next->mo_store_##x(		\
	try_const_addr(addr), v); }

MMU_ACCESS_ALL()

DECL_MMUOPS_S(constchk);

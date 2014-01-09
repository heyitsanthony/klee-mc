#include "klee/klee.h"
#include "mmu_testptr.h"
#include "mmu.h"

#define MMU_LOAD(x,y)		\
y mmu_load_##x##_forkall(void* addr)	\
{	y		*p;	\
	uint64_t	c_64;	\
	mmu_testptr(addr);			\
	c_64 = klee_fork_all(addr);		\
	p = (y*)c_64;				\
	return *p; }

#define MMU_STORE(x,y)			\
void mmu_store_##x##_forkall(void* addr, y v)	\
{	y *p;					\
	uint64_t	c_64;			\
	mmu_testptr(addr);			\
	c_64 = klee_fork_all(addr);		\
	p = (y*)c_64;				\
	*p = v;	}

MMU_ACCESS_ALL();
DECL_MMUOPS_S(forkall);
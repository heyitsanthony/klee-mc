#include "klee/klee.h"
#include "mmu_testptr.h"
#include "mmu.h"

#define MMU_LOAD(x,y)		\
y mmu_load_##x##_forkall(void* addr)	\
{	uint64_t	c_64;		\
	mmu_testptr(addr);		\
	c_64 = klee_fork_all(addr);	\
	return *((y*)c_64); }

#define MMU_STORE(x,y)			\
void mmu_store_##x##_forkall(void* addr, y v)	\
{	uint64_t	c_64;			\
	mmu_testptr(addr);			\
	c_64 = klee_fork_all(addr);		\
	*((y*)c_64) = v; }

MMU_ACCESS_ALL();
DECL_MMUOPS_S(forkall);
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

#define MMU_ACCESS(x,y)	\
	MMU_LOAD(x,y)	\
	MMU_STORE(x,y)

MMU_ACCESS(8, uint8_t)
MMU_ACCESS(16, uint16_t)
MMU_ACCESS(32, uint32_t)
MMU_ACCESS(64, uint64_t)
MMU_ACCESS(128, __uint128_t)

DECL_MMUOPS_S(forkall);
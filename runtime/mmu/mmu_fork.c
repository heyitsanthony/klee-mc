#include "klee/klee.h"
#include "mmu_testptr.h"
#include "mmu.h"

#define MAX_FORKS	10

#define MMU_LOAD(x,y)		\
y mmu_load_##x##_fork(void* addr)	\
{	y		*p;	\
	int		i = 0;	\
	uint64_t	a_64 = (uint64_t)addr, c_64;	\
	mmu_testptr(addr);			\
	c_64 = klee_get_value(a_64);		\
	do {					\
		c_64 = klee_get_value(a_64);	\
	 	if (i == MAX_FORKS) break;	\
		i++;				\
	} while (a_64 != c_64); 		\
	klee_assume_eq (a_64, c_64);		\
	p = (y*)c_64;				\
	return *p; }

#define MMU_STORE(x,y)			\
void mmu_store_##x##_fork(void* addr, y v)	\
{	y *p;					\
	int		i = 0;			\
	uint64_t	a_64 = (uint64_t)addr, c_64;	\
	mmu_testptr(addr);			\
	do { 	c_64 = klee_get_value(a_64);	\
	 	if (i == MAX_FORKS) break;	\
		i++;				\
	} while (a_64 != c_64); 		\
	klee_assume_eq (a_64, c_64);		\
	p = (y*)c_64;				\
	*p = v;	}

MMU_ACCESS_ALL();
DECL_MMUOPS_S(fork);
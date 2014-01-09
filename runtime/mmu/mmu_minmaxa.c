#include "klee/klee.h"
#include "mmu_testptr.h"
#include "mmu.h"

#define SETUP_ADDR()	\
	uint64_t	a_64 = (uint64_t)addr, c_64;	\
	c_64 = klee_min_value(a_64);		\
	if (c_64 != a_64)			\
		c_64 = klee_max_value(a_64);	\
	klee_assume_eq (a_64, c_64);

#define MMU_LOAD(x,y)		\
y mmu_load_##x##_minmaxa(void* addr)	\
{	y		*p;	\
	SETUP_ADDR();		\
	p = (y*)c_64;		\
	return *p; }

#define MMU_STORE(x,y)			\
void mmu_store_##x##_minmaxa(void* addr, y v)\
{	y *p;					\
	SETUP_ADDR();				\
	p = (y*)c_64;				\
	*p = v;	}

MMU_ACCESS_ALL();
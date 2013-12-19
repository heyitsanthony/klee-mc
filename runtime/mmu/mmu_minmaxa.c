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

#define MMU_ACCESS(x,y)	\
	MMU_LOAD(x,y)	\
	MMU_STORE(x,y)

MMU_ACCESS(8, uint8_t)
MMU_ACCESS(16, uint16_t)
MMU_ACCESS(32, uint32_t)
MMU_ACCESS(64, uint64_t)
MMU_ACCESS(128, __uint128_t)
#include "klee/klee.h"
#include "mmu_testptr.h"
#include "mmu.h"

#define MMU_LOAD(x,y)		\
y mmu_load_##x##_uniqptr(void* addr)	\
{	uint64_t	a_64 = (uint64_t)addr, c_64;	\
	mmu_testptr(addr);			\
	c_64 = klee_get_value(a_64);		\
	klee_assume_eq (a_64, c_64);		\
	return *((y*)c_64); }			\

#define MMU_STORE(x,y)			\
void mmu_store_##x##_uniqptr(void* addr, y v)	\
{	uint64_t	a_64 = (uint64_t)addr, c_64;	\
	mmu_testptr(addr);			\
	c_64 = klee_get_value(a_64);		\
	klee_assume_eq (a_64, c_64);		\
	*((y*)c_64) = v; }

MMU_ACCESS_ALL();

DECL_MMUOPS_S(uniqptr);
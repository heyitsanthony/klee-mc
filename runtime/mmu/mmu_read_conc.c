/*
 * concretizes all constants going to and from memory
 */
#include "klee/klee.h"
#include "mmu.h"


#define CHK_V(t, v)	do {	\
	t c = klee_get_value(v);	\
	if (klee_valid_eq(v, c)) {	\
		klee_assume_eq(v, c);	\
		v = c; } } while (0)

#define MMU_LOADC(x,y)		\
y mmu_load_##x##_const2concc(void* addr)	\
{	y ret = mmu_load_##x##_cnulltlb(addr);	\
	CHK_V(y, ret);	\
	return ret; }

#define MMU_STOREC(x,y)			\
void mmu_store_##x##_const2concc(void* addr, y v)	\
{	CHK_V(y, v);				\
	return mmu_store_##x##_cnulltlb(addr, v); }

#define MMU_LOAD(x,y)		\
y mmu_load_##x##_const2conc(void* addr)	\
{ y ret = mmu_load_##x##_objwide(addr);	\
  CHK_V(y, ret);			\
  return ret; }

#define MMU_STORE(x,y)			\
void mmu_store_##x##_const2conc(void* addr, y v)	\
{	CHK_V(y, v);				\
	mmu_store_##x##_objwide(addr, v); }


#define MMU_ACCESS(x,y)			\
	MMU_LOAD(x,y)	MMU_LOADC(x,y)	\
	MMU_STORE(x,y)	MMU_STOREC(x,y)

MMU_ACCESS(8, uint8_t)
MMU_ACCESS(16, uint16_t)
MMU_ACCESS(32, uint32_t)
MMU_ACCESS(64, uint64_t)
MMU_ACCESS(128, __uint128_t)

DECL_MMUOPS_S(const2conc);
DECL_MMUOPS_S(const2concc);

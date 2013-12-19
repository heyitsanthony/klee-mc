/* main idea: keep shadow memory for all
 * memory accesses. greedily maximize address locations
 * to have the most accesses or least accesses */
#include "klee/klee.h"
#include "mmu.h"

#define MMU_LOADC(x,y)		\
y mmu_load_##x##_templatec(void* addr)	\
{ return mmu_load_##x##_cnulltlb(addr); }

#define MMU_STOREC(x,y)			\
void mmu_store_##x##_templatec(void* addr, y v)	\
{ return mmu_store_##x##_cnulltlb(addr, v); }

#define MMU_LOAD(x,y)		\
y mmu_load_##x##_template(void* addr)	\
{ return mmu_load_##x##_objwide(addr); }

#define MMU_STORE(x,y)			\
void mmu_store_##x##_template(void* addr, y v)	\
{ return mmu_store_##x##_objwide(addr, v); }


#define MMU_ACCESS(x,y)			\
	MMU_LOAD(x,y)	MMU_LOADC(x,y)	\
	MMU_STORE(x,y)	MMU_STOREC(x,y)

MMU_ACCESS(8, uint8_t)
MMU_ACCESS(16, uint16_t)
MMU_ACCESS(32, uint32_t)
MMU_ACCESS(64, uint64_t)
MMU_ACCESS(128, __uint128_t)

DECL_MMUOPS_S(template);

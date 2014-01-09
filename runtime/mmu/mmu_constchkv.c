#include "klee/klee.h"
#include "mmu.h"


MMUOPS_S_EXTERN(constchkv);
MMUOPS_S_EXTERN(constchkvc);

#define CHK_V(t, v)	\
do {	\
	t c = klee_get_value(v);	\
	if (klee_valid_eq(v, c)) {	\
		klee_assume_eq(v, c);	\
		v = c;	}		\
} while (0)


#define MMU_LOADC(x,y)		\
y mmu_load_##x##_constchkvc(void* addr)	\
{	y ret = MMUOPS_S(constchkvc).mo_next->mo_load_##x(addr); \
	CHK_V(y, ret);	\
	return ret; }

#define MMU_STOREC(x,y)			\
void mmu_store_##x##_constchkvc(void* addr, y v)	\
{	CHK_V(y, v);				\
	return MMUOPS_S(constchkv).mo_next->mo_store_##x(addr,v); }

#define MMU_LOAD(x,y)		\
y mmu_load_##x##_constchkv(void* addr)	\
{	y ret = MMUOPS_S(constchkv).mo_next->mo_load_##x(addr);	\
	CHK_V(y, ret);	\
	return ret; }

#define MMU_STORE(x,y)			\
void mmu_store_##x##_constchkv(void* addr, y v)	\
{	CHK_V(y, v);			\
	MMUOPS_S(constchkv).mo_next->mo_store_##x(addr, v); }


#undef MMU_ACCESS
#define MMU_ACCESS(x,y)			\
	MMU_LOAD(x,y)	MMU_LOADC(x,y)	\
	MMU_STORE(x,y)	MMU_STOREC(x,y)

MMU_ACCESS(8, uint8_t);
MMU_ACCESS(16, uint16_t);
MMU_ACCESS(32, uint32_t);
MMU_ACCESS(64, uint64_t);

void mmu_store_128_constchkv(void* addr, __uint128_t v)
{ MMUOPS_S(constchkv).mo_next->mo_store_128(addr, v);}

void mmu_store_128_constchkvc(void* addr, __uint128_t v)
{ MMUOPS_S(constchkvc).mo_next->mo_store_128(addr, v); }

__uint128_t mmu_load_128_constchkv(void* addr)
{ return MMUOPS_S(constchkv).mo_next->mo_load_128(addr);}

__uint128_t mmu_load_128_constchkvc(void* addr)
{ return MMUOPS_S(constchkvc).mo_next->mo_load_128(addr); }

DECL_MMUOPS_S(constchkv);
DECL_MMUOPS_S(constchkvc);

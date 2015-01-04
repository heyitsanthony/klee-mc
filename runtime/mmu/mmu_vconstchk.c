#include "klee/klee.h"
#include "mmu.h"

// According to scripts/err/early.sh on testbeds, this causes approximately
// 20% of ALL early tests to timeout. The validity calls turn out to be
// quite expensive and timeout.
//
// It's not clear to me whether there's a huge performance win, but I doubt it.
// Rules should soak up tautologies and context-dependent validity should be rare.
//
// To ensure this doesn't ruin the symbolic execution, in the future there needs
// to be a way to at least avoid erroring-out on solver timeouts for speculative
// policies like these...
//


MMUOPS_S_EXTERN(vconstchk);
MMUOPS_S_EXTERN(vconstchkc);

#define CHK_V(t, v)	\
do {	\
	t c = klee_get_value(v);	\
	if (klee_valid_eq(v, c)) {	\
		klee_assume_eq(v, c);	\
		v = c;	}		\
} while (0)


#define MMU_LOADC(x,y)		\
y mmu_load_##x##_vconstchkc(void* addr)	\
{	y ret = MMUOPS_S(vconstchkc).mo_next->mo_load_##x(addr); \
	CHK_V(y, ret);	\
	return ret; }

#define MMU_STOREC(x,y)			\
void mmu_store_##x##_vconstchkc(void* addr, y v)	\
{	CHK_V(y, v);				\
	return MMUOPS_S(vconstchk).mo_next->mo_store_##x(addr,v); }

#define MMU_LOAD(x,y)		\
y mmu_load_##x##_vconstchk(void* addr)	\
{	y ret = MMUOPS_S(vconstchk).mo_next->mo_load_##x(addr);	\
	CHK_V(y, ret);	\
	return ret; }

#define MMU_STORE(x,y)			\
void mmu_store_##x##_vconstchk(void* addr, y v)	\
{	CHK_V(y, v);			\
	MMUOPS_S(vconstchk).mo_next->mo_store_##x(addr, v); }


#undef MMU_ACCESS
#define MMU_ACCESS(x,y)			\
	MMU_LOAD(x,y)	MMU_LOADC(x,y)	\
	MMU_STORE(x,y)	MMU_STOREC(x,y)

MMU_ACCESS(8, uint8_t);
MMU_ACCESS(16, uint16_t);
MMU_ACCESS(32, uint32_t);
MMU_ACCESS(64, uint64_t);

void mmu_store_128_vconstchk(void* addr, __uint128_t v)
{ MMUOPS_S(vconstchk).mo_next->mo_store_128(addr, v);}

void mmu_store_128_vconstchkc(void* addr, __uint128_t v)
{ MMUOPS_S(vconstchkc).mo_next->mo_store_128(addr, v); }

__uint128_t mmu_load_128_vconstchk(void* addr)
{ return MMUOPS_S(vconstchk).mo_next->mo_load_128(addr);}

__uint128_t mmu_load_128_vconstchkc(void* addr)
{ return MMUOPS_S(vconstchkc).mo_next->mo_load_128(addr); }

DECL_MMUOPS_S(vconstchk);
DECL_MMUOPS_S(vconstchkc);

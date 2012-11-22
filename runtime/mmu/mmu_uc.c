/* XXX: this doesn't do anything yet */
#include "klee/klee.h"
#include "mmu_testptr.h"
#include "mmu.h"

static int is_uc_ptr(void* addr)
{
	/* XXX: how to distinguish UC? no solver queries! */
	klee_uerror("STUB: is_uc_ptr", "stub.err");
	return 0;
}

static uint64_t get_uc_ptr(void* addr)
{
	/* what do I do here */
	klee_uerror("STUB: get_uc_ptr", "stub.err");
	return 0;
}

#define MMU_LOAD(x,y,z)		\
y mmu_load_##x##_uc(void* addr)	\
{	y		*p;	\
	uint64_t	c_64;		\
	if (!is_uc_ptr(addr))	\
		return mmu_load_ ##x## _ ##z (addr);	\
	c_64 = get_uc_ptr(addr);		\
	p = (y*)c_64;				\
	return *p; }

#define MMU_STORE(x,y,z)			\
void mmu_store_##x##_uc(void* addr, y v)	\
{	y *p;			\
	uint64_t	c_64;	\
	if (!is_uc_ptr(addr)) {			\
		mmu_store_##x##_##z (addr, v); 	\
		return;				\
	}					\
	c_64 = get_uc_ptr(addr);		\
	p = (y*)c_64;		\
	*p = v;	}

#define MMU_ACCESS(x,y)	\
	MMU_LOAD(x,y,objwide)	\
	MMU_STORE(x,y,objwide)

MMU_ACCESS(8, uint8_t)
MMU_ACCESS(16, uint16_t)
MMU_ACCESS(32, uint32_t)
MMU_ACCESS(64, uint64_t)
MMU_ACCESS(128, __uint128_t)
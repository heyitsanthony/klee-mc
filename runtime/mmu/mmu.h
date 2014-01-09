#ifndef RT_SYMMMU_H
#define RT_SYMMMU_H

#include <stdint.h>

/* ex. mmu_load_8_objwide */
#define DEF_LOAD(x, y)		y mmu_load_##x(void* addr);
#define DEF_STORE(x, y)		void mmu_store_##x(void* addr, y v);
#define DEF_ACCESS(a,x,y)	\
	DEF_LOAD(x ## _ ## a,y);	\
	DEF_STORE(x ## _ ## a,y);

#define DEF_MMU(a)			\
	DEF_ACCESS(a, 8, uint8_t)	\
	DEF_ACCESS(a, 16, uint16_t)	\
	DEF_ACCESS(a, 32, uint32_t)	\
	DEF_ACCESS(a, 64, uint64_t)	\
	DEF_ACCESS(a, 128, __uint128_t)
DEF_MMU(uniqptr)
DEF_MMU(null)
DEF_MMU(fork)
DEF_MMU(forkall)
DEF_MMU(forkobj)
DEF_MMU(objwide)
DEF_MMU(uc)
DEF_MMU(inst)
DEF_MMU(cnulltlb)
DEF_MMU(cnull)

#define MMUOPS_S(name)	mmu_ops_##name
#define MMUOPS_S_EXTERN(name)	extern struct mmu_ops MMUOPS_S(name)

#define DECL_MMUOPS_W(name, w)	\
	.mo_store_##w = mmu_store_##w##_##name,	\
	.mo_load_##w = mmu_load_##w##_##name,
#define DECL_MMUOPS(name)		\
	.mo_next = NULL,		\
	DECL_MMUOPS_W(name, 8)		\
	DECL_MMUOPS_W(name, 16)		\
	DECL_MMUOPS_W(name, 32)		\
	DECL_MMUOPS_W(name, 64)		\
	DECL_MMUOPS_W(name, 128)
#define DECL_MMUOPS_ALL(name)		\
	DECL_MMUOPS(name)		\
	.mo_signal = NULL,		\
	.mo_cleanup = NULL,		\
	.mo_init = NULL
#define DECL_MMUOPS_S(name)		\
struct mmu_ops MMUOPS_S(name) = { DECL_MMUOPS_ALL(name) }

struct mmu_ops
{
	/* mo_next is first so it's easier to construct the
	 * mmu stack in the interpreter */
	struct mmu_ops	*mo_next;
#define DECL_MMUOP(w, type)	\
	void (*mo_store_##w)(void* addr, type v);	\
	type (*mo_load_##w)(void* addr);

	DECL_MMUOP(8, uint8_t)
	DECL_MMUOP(16, uint16_t)
	DECL_MMUOP(32, uint32_t)
	DECL_MMUOP(64, uint64_t)
	DECL_MMUOP(128, __uint128_t)

	void (*mo_signal)(void* addr, uint64_t len);
	void (*mo_cleanup)(void);
	void (*mo_init)(void);
};

/* MMU_LOAD and MMU_STORE are defined by mmu_<handler>.c */
#define MMU_ACCESS(x,y)	MMU_LOAD(x,y) MMU_STORE(x,y)
#define MMU_ACCESS_ALL()		\
	MMU_ACCESS(8, uint8_t)		\
	MMU_ACCESS(16, uint16_t)	\
	MMU_ACCESS(32, uint32_t)	\
	MMU_ACCESS(64, uint64_t)	\
	MMU_ACCESS(128, __uint128_t)

#endif

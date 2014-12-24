/**
 * hides/masks all side effects from execution;
 * restores program memory when unmasked
 *
 */
#include "klee/klee.h"
#include "mmu.h"
#include "shadow.h"

MMUOPS_S_EXTERN(mask);
MMUOPS_S_EXTERN(maskc);

#if 0
//#warning mmu_mask is not done yet

static int			mmu_mask_c = 0;
static int			in_mmu_mask = 0;
static struct shadow_info	mmu_mask_si;

void mmu_mask_mask(void)
{
	mmu_mask_c++;
	if (mmu_mask_c > 1) return;
	shadow_init(&mmu_mask_si, 64, 64, 0);	
}

static void mmu_mask_copyin(void)
{
	void	*spg_prev = NULL, *spg_cur;

	/* copy shadow memory back into main memory */
	while ((spg_cur = shadow_next_pg(&mmu_mask_si, spg_prev)) != NULL) {
		int	i;
		for (i = 0; i < SHADOW_PG_SZ / 8; i++)
			*((uint64_t*)spg_cur) = shadow_get(
				&mmu_mask_si, ((intptr_t)spg_cur)+i*8);
		spg_prev = spg_cur;
	}
}

void mmu_mask_unmask(void)
{
	if (mmu_mask_c == 0)
		klee_uerror("Unmasking without mask enabled", "mask.err");

	mmu_mask_c--;
	if (mmu_mask_c != 0) return;

	mmu_mask_copyin();
	shadow_fini(&mmu_mask_si);
}

void mmu_signal_mask(void* addr, unsigned len)
{
	/* XXX: klee_tlb_invalidate(NULL) should invalidate all;
	 * make sure this functionality works */
	klee_tlb_invalidate(NULL, ~0);
}

#define MMU_LOADC(x,y)		\
y mmu_load_##x##_maskc(void* addr)	\
{	if (in_mmu_mask) return MMU_FWD_LOAD(maskc, x, addr);	\
	in_mmu_mask++;	\
	/* XXX */	\
	in_mmu_mask--; }

#define MMU_STOREC(x,y)			\
void mmu_store_##x##_maskc(void* addr, y v)	\
{ if(in_mmu_mask) return MMU_FWD_STORE(maskc, x, addr, v); \
in_mmu_mask++; \
/* XXX */ \
in_mmu_mask--; }

#define MMU_LOAD(x,y)		\
y mmu_load_##x##_mask(void* addr)	\
{ if (in_mmu_mask) return MMU_FWD_LOAD(mask, x, addr); \
in_mmu_mask++;	\
/* XXX */	\
in_mmu_mask--; }

#define MMU_STORE(x,y)			\
void mmu_store_##x##_mask(void* addr, y v)	\
{ if (in_mmu_mask) return MMU_FWD_STORE(mask, x, addr, v); \
in_mmu_mask++;	\
/* XXX */	\
in_mmu_mask--; }

#undef MMU_ACCESS
#define MMU_ACCESS(x,y)			\
	MMU_LOAD(x,y)	MMU_LOADC(x,y)	\
	MMU_STORE(x,y)	MMU_STOREC(x,y)

MMU_ACCESS_ALL();
DECL_MMUOPS_S(maskc);
DECL_MMUOPS_S(mask);
#endif

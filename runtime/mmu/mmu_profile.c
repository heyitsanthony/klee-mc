/* main idea: keep shadow memory for all
 * memory accesses. greedily maximize address locations
 * to have the most accesses or least accesses */

/* we don't care about concrete accesses for profiling */

#include "klee/klee.h"
#include "mmu.h"
#include "shadow.h"

static struct shadow_info	profile_si;

static uint8_t in_profile = 0;

#define PROF_BYTES	8

#define MMU_LOAD(x,y)		\
y mmu_load_##x##_profile(void* addr)	\
{	if (! in_profile) {	\
		in_profile++;	\
		shadow_put(	\
			&profile_si, 	\
			(long)addr,	\
			shadow_get(&profile_si, (long)addr) + 1);	\
		in_profile--;	\
	}	\
	return mmu_load_##x##_objwide(addr); }

#define MMU_STORE(x,y)			\
void mmu_store_##x##_profile(void* addr, y v)	\
{	\
	if (! in_profile) {	\
		in_profile++;	\
		shadow_put(	\
			&profile_si, 	\
			(long)addr,	\
			shadow_get(&profile_si, (long)addr) + 1);	\
		in_profile--;	\
	}	\
	return mmu_store_##x##_objwide(addr, v); }

void mmu_init_profile(void) { shadow_init(&profile_si, PROF_BYTES, 32, 0); }

void mmu_cleanup_profile(void)
{
	void	*cur_pg = NULL;
	unsigned	sym_c = 0;

	klee_print_expr("[Profile] Minimizing", 0);

	while ((cur_pg = shadow_next_pg(&profile_si, cur_pg)) != NULL) {
		unsigned	i;
		unsigned	lo, hi;

#if 0
		if (!shadow_used_range(&profile_si, (long)cur_pg, &lo, &hi))
			continue;

		klee_print_expr("lo", lo);
		klee_print_expr("hi", hi);
#else
		lo = 0;
		hi = 4096*4/8;
#endif

		/* divide by 4 to get unit number */
		for (i = lo/4; i < hi/4; i++) {
			uint64_t	v;

			/* prof bytes is unit address stride */
			v = shadow_get(&profile_si, (long)cur_pg+i*PROF_BYTES);
			if (!klee_is_symbolic(v))
				continue;

			if (klee_feasible_ugt(v, 1))
				klee_assume_ugt(v, 1);
		}
	}

	klee_print_expr("Profile accesses minimized", sym_c);
}

#define MMU_ACCESS(x,y)			\
	MMU_LOAD(x,y) MMU_STORE(x,y)

MMU_ACCESS(8, uint8_t)
MMU_ACCESS(16, uint16_t)
MMU_ACCESS(32, uint32_t)
MMU_ACCESS(64, uint64_t)
MMU_ACCESS(128, __uint128_t)

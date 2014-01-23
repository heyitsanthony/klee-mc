/* main idea: keep shadow memory for all
 * memory accesses. greedily maximize address locations
 * to have the most accesses or least accesses */

/* we don't care about concrete accesses for profiling */

#include "klee/klee.h"
#include "mmu.h"
#include "shadow.h"
#include "../Intrinsic/cex.h"

MMUOPS_S_EXTERN(profile);

static struct shadow_info	profile_si;
static uint8_t			in_profile = 0;
static unsigned			load_update_c = 0 , store_update_c = 0;
static struct cex_t		profile_cex;

#define PROF_BYTES	4	/* words */

static int try_cex(uint64_t addr, uint64_t a_r, unsigned i)
{	uint64_t ca = cex_get(&profile_cex, i);
	if (shadow_get(&profile_si, ca)) return 0;
	if (!cex_take(&profile_cex, a_r, i)) return 0;
	if (klee_feasible_eq(addr, ca)) klee_assume_eq(addr, ca);
	else klee_assume_eq(a_r, ca);
	shadow_put(&profile_si, ca, 1);
	return 1; }

static void greedy_update(uint64_t addr)
{	uint64_t a_r = addr & ~(PROF_BYTES-1LL);
	if (cex_n(&profile_cex) && try_cex(addr, a_r, 0)) return;
	int n = cex_find(&profile_cex, a_r), i;
	for (i = 0; i < n; i++) if (try_cex(addr, a_r, i)) return;
	/* slooow path */
	cex_flush(&profile_cex);
	shadow_put(&profile_si, addr, shadow_get(&profile_si, addr)+1); }

#define INC_SHADOW(x,a)	\
do {	if (in_profile) break;	\
	x##_update_c++;		\
	in_profile++; greedy_update((uint64_t)a); in_profile--;	\
} while (0)

#define MMU_LOAD(x,y)		\
y mmu_load_##x##_profile(void* addr)	\
{ INC_SHADOW(load, addr); return MMU_FWD_LOAD(profile, x, addr); }

#define MMU_STORE(x,y)			\
void mmu_store_##x##_profile(void* addr, y v)	\
{ INC_SHADOW(store, addr); return MMU_FWD_STORE(profile, x, addr, v); }

#define UNIT_BITS	32
#define UNIT_BYTES	(UNIT_BITS/8)
void mmu_init_profile(void) {
	cex_init(&profile_cex, 2048);
	shadow_init(&profile_si, PROF_BYTES, UNIT_BITS, 0);
	MMU_FWD_INIT(profile); }

void mmu_cleanup_profile(void)
{
	void		*cur_pg = NULL;
	unsigned	sym_c = 0, shadow_pg_c = 0;

	in_profile++;
	klee_print_expr("[mmu_profile] Minimizing", 0);

	while ((cur_pg = shadow_next_pg(&profile_si, cur_pg)) != NULL) {
		unsigned	i;
		for (i = 0; i < 4096*PROF_BYTES/UNIT_BYTES; i++) {
			uint64_t	v;

			/* prof bytes is unit address stride */
			v = shadow_get(&profile_si, (long)cur_pg+i*PROF_BYTES);
			if (!klee_is_symbolic(v) || !klee_feasible_ugt(v,1))
				continue;

			klee_assume_ugt(v, 1);
			sym_c++;
		}

		shadow_pg_c++;
	}

	klee_print_expr("[mmu_profile] accesses", load_update_c+store_update_c);
	klee_print_expr("[mmu_profile] shadow pages processed", shadow_pg_c);
	klee_print_expr("[mmu_profile] accesses minimized", sym_c);

	MMU_FWD_CLEANUP(profile);
}

MMU_ACCESS_ALL();

struct mmu_ops mmu_ops_profile = {
	DECL_MMUOPS(profile)
	.mo_signal = NULL,
	.mo_cleanup = mmu_cleanup_profile,
	.mo_init = mmu_init_profile };

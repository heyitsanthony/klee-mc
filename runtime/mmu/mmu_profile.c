/* main idea: keep shadow memory for all
 * memory accesses. greedily maximize address locations
 * to have the most accesses or least accesses */

/* we don't care about concrete accesses for profiling */

#include "klee/klee.h"
#include "mmu.h"
#include "shadow.h"

static struct shadow_info	profile_si;

#define MMU_LOAD(x,y)		\
y mmu_load_##x##_profile(void* addr)	\
{	shadow_put(	\
		&profile_si, 	\
		(long)addr,	\
		shadow_get(&profile_si, (long)addr) + 1);	\
	return mmu_load_##x##_objwide(addr); }

#define MMU_STORE(x,y)			\
void mmu_store_##x##_profile(void* addr, y v)	\
{	\
	shadow_put(	\
		&profile_si, 	\
		(long)addr,	\
		shadow_get(&profile_si, (long)addr) + 1);	\
	return mmu_store_##x##_objwide(addr, v); }

void mmu_init_profile(void) { shadow_init(&profile_si, 1, 32, 0); }

void mmu_fini_profile(void)
{
	void	*cur_pg = NULL;
	unsigned	sym_c = 0;

	while ((cur_pg = shadow_next_pg(&profile_si, cur_pg)) != NULL) {
		unsigned	i;
		for (i = 0; i < 4096; i++) {
			uint64_t	v, m_c;

			v = shadow_get(&profile_si, (long)cur_pg + i);
			if (!klee_is_symbolic(v))
				continue;

			m_c = klee_max_value(v);
			klee_assume_eq(v, m_c);
			sym_c++;
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

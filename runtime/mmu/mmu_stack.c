#include "klee/klee.h"
#include "mmu.h"

static uint64_t stack_begin = 0, stack_end = 0;

MMUOPS_S_EXTERN(stack);

static struct kreport_ent rosmash_ktab[] =
{	MK_KREPORT("sym-address"),
	MK_KREPORT("stk-address"),
	MK_KREPORT("value-expr"),
	MK_KREPORT("value-const"),
	MK_KREPORT(NULL) };


static void stack_load(void)
{
	/* initialized? */
	if (stack_begin) return;
	stack_begin = klee_read_reg("stack_begin");
	stack_end = klee_read_reg("stack_end");
}

static void* find_ret_addr(void* addr)
{
	void		*stk_top, *stk_bottom;
	unsigned	i, len;

	/* most recent point on the stack */
	stk_top = (void*)klee_min_value((uint64_t)addr);
	if (klee_valid_eq(addr, stk_top))
		return stk_top;

	/* least recent point on the stack */
	stk_bottom  = (void*)klee_max_value((uint64_t)addr);

	/* find everything that looks like an address in the range;
	 * iterate from bottom (most likely to be smashy, to the top) */
	len = (uintptr_t)stk_bottom - (uintptr_t)stk_top;
	for (i = 0; i < len; i++) {
		uint64_t	*cur_addr, v, v_c;
		uint64_t	not_code_cond;

		cur_addr = (uint64_t*)((char*)stk_bottom - i);
		v = *cur_addr;

		not_code_cond =
			klee_mk_or(
				klee_mk_ne(v & (1UL << 63), 0),
				klee_mk_ule(v, 0x400000));

		/* check for bad address -- notcodecond != 0 => 
		 * is code cond */
		if (klee_valid_ne(not_code_cond, 0)) continue;

		v_c = klee_get_value(v);

		/* code addresses are read-only */
		if (!klee_is_readonly((const void*)v_c)) continue;

		if (klee_feasible_eq(addr, cur_addr)) {
			SET_KREPORT(&rosmash_ktab[0], addr);
			SET_KREPORT(&rosmash_ktab[1], cur_addr);
			SET_KREPORT(&rosmash_ktab[2], v);
			SET_KREPORT(&rosmash_ktab[3], v_c);

			klee_assume_eq(cur_addr, addr);
			klee_ureport_details(
				"overwrote read-only address on stack",
				"smash.warning",
				&rosmash_ktab);
			return cur_addr;
		}
	}

	return NULL;
}

#define MMU_LOAD(x,y)			\
y mmu_load_##x##_stack(void* addr)	\
{ return MMUOPS_S(stack).mo_next->mo_load_##x(addr); }

#define IN_STACK(x)	((x) >= stack_begin && (x) <= stack_end)

#define MMU_STORE(x,y)				\
void mmu_store_##x##_stack(void* addr, y v)	\
{	void* target_addr;			\
	stack_load();			\
	if (!IN_STACK((uint64_t)addr)) {		\
		MMUOPS_S(stack).mo_next->mo_store_##x(addr, v); \
		return;	\
	}	\
	if ((target_addr = find_ret_addr(addr))) *((y*)target_addr) = v; }

MMU_ACCESS_ALL();
DECL_MMUOPS_S(stack);
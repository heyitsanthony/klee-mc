#include "klee/klee.h"
#include "mmu.h"

static uint64_t stack_begin = 0, stack_end = 0;

MMUOPS_S_EXTERN(stack);

static struct kreport_ent rosmash_ktab[] =
{	MK_KREPORT("sym-address"),
	MK_KREPORT("stk-address"),
	MK_KREPORT("value-expr"),
	MK_KREPORT("value-const"),
	MK_KREPORT("write-expr"),
	MK_KREPORT(NULL) };


static void stack_load(void)
{
	/* initialized? */
	if (stack_begin) return;
	stack_begin = klee_read_reg("stack_begin");
	stack_end = klee_read_reg("stack_end");
}


#define MAX_RET_ADDR	16
static uint64_t	ret_addr_buf[MAX_RET_ADDR];

extern mmu_load_64_objwideS(const void* addr);

static void* find_ret_addr_wide(void* addr, uint64_t write_v)
{
	uint64_t	v, is_codeaddr_cond;
	unsigned	i, n;

	v = mmu_load_64_objwideS(addr);

	is_codeaddr_cond =
		klee_mk_and(
			klee_mk_eq(v & (1UL << 63), 0),
			klee_mk_ugt(v, 0x400000));

	/* check for bad address -- notcodecond != 0 => is code cond */
	if (!__klee_feasible(is_codeaddr_cond)) return NULL;

	n = klee_get_values_pred(
		v, ret_addr_buf, MAX_RET_ADDR, is_codeaddr_cond);

	for (i = 0; i < n; i++) {
		if (klee_is_readonly((void*)ret_addr_buf[i]) != 1)
			continue;
		if (!klee_prefer_eq(v, ret_addr_buf[i])) continue;

		SET_KREPORT(&rosmash_ktab[0], addr);
		SET_KREPORT(&rosmash_ktab[1], addr);
		SET_KREPORT(&rosmash_ktab[2], v);
		SET_KREPORT(&rosmash_ktab[3], ret_addr_buf[i]);
		SET_KREPORT(&rosmash_ktab[4], write_v);

		klee_assume_eq(v, ret_addr_buf[i]);

		klee_ureport_details(
			"overwrote read-only address on stack",
			"smash.warning",
			&rosmash_ktab);

		return addr;
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
	if (IN_STACK((uint64_t)addr)) {		\
		target_addr = find_ret_addr_wide(addr,v);	\
		if (target_addr) addr = target_addr; }		\
	MMUOPS_S(stack).mo_next->mo_store_##x(addr, v); }

MMU_ACCESS_ALL();
DECL_MMUOPS_S(stack);
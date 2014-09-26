#include "klee/klee.h"
#include "mmu.h"
#define GUEST_ARCH_AMD64
#include "../syscall/syscalls.h"

MMUOPS_S_EXTERN(stack2s);
MMUOPS_S_EXTERN(stack2c);

static struct kreport_ent rosmash_ktab[] =
{	MK_KREPORT("write-address"),
	MK_KREPORT("cur-stk-ptr"),
	MK_KREPORT("write-value-expr"),
	MK_KREPORT("write-value-const"),
	MK_KREPORT("stk-value-expr"),
	MK_KREPORT(NULL) };


#define MAX_STACK_DEPTH	32
static void* ret_addr_locs[MAX_STACK_DEPTH+1];

static int try_tlb(void* addr)
{
	uint64_t sp = GET_STACK(kmc_regs_get());
	const void* ins_addr;

	/* possibly in stack? */
	if (	(uint64_t)addr > sp-0x100000 && 
		(uint64_t)addr < sp+0x100000)
		return 0;

	ins_addr = (void*)((uint64_t)addr & ~(0xfffUL));
	if (!klee_is_valid_addr(ins_addr))
		return 1;

	klee_tlb_insert(ins_addr, 4096);
	return 1;
}

static void chk_touch_prior_ptr(void* addr, unsigned w, uint64_t write_v)
{
	uint64_t pred, v, cur_stk_ptr;
	int depth = klee_stack_depth();

	 /* it should be more than this, but I don't have a good
	  * way to measure the depth of the MMU stack */
	depth -= 2;
	if (depth < 0) return;

	cur_stk_ptr = GET_STACK(kmc_regs_get());

	while (depth >= 0) {
		v = (uint64_t)ret_addr_locs[depth];

		/* skip out-of-date addrs-- rsp > old_rsp =>
		 * data no longer valid  */
		if (klee_valid_ugt(cur_stk_ptr, v)) {
			depth--;
			continue;
		}

		/* NOTE: addr_loc_i < addr_loc_{i-1} < ... < addr_loc_0 */
		if (klee_valid_ult((uint64_t)addr-w, v)) {
			/* addr-w < addr_loc_k => 
			 * addr-w < addr_loc_j for j <= k
			 * hence, no matching addresses from here on out... */
			return;
		}

		/* in range of addr? */
		pred = klee_mk_and(
			klee_mk_ult((uint64_t)addr-w, v),
			klee_mk_uge((uint64_t)addr+w, v));
		if (__klee_feasible(pred)) break;

		depth--;
	}

	if (depth < 0) return;

	klee_print_expr("addr-w", addr-w);
	klee_print_expr("addr+w", addr+w);
	klee_print_expr("ret_addr_loc",  (uint64_t)ret_addr_locs[depth]);


	/* XXX: should really fork if feasible... */
	__klee_assume(pred);

	/* XXX: not quite, needs to check any overlap */
	SET_KREPORT(&rosmash_ktab[0], addr);
	SET_KREPORT(&rosmash_ktab[1], cur_stk_ptr);
	SET_KREPORT(&rosmash_ktab[2], write_v);
	SET_KREPORT(&rosmash_ktab[3], klee_get_value(write_v));
	SET_KREPORT(&rosmash_ktab[4], v); 
	klee_ureport_details(
		"overwrote read-only address on stack",	
		"smash.warning",
		&rosmash_ktab);	
}

#define MMU_LOAD(x,y)			\
y mmu_load_##x##_stack2s(void* addr)	\
{ return MMUOPS_S(stack2s).mo_next->mo_load_##x(addr); }

#define MMU_STORE(x,y)				\
void mmu_store_##x##_stack2s(void* addr, y v)	\
{	chk_touch_prior_ptr(addr, x/8, v);	\
	MMUOPS_S(stack2s).mo_next->mo_store_##x(addr, v); }

MMU_ACCESS_ALL();
DECL_MMUOPS_S(stack2s);

#undef MMU_LOAD
#undef MMU_STORE			

#define MMU_LOAD(x,y)			\
y mmu_load_##x##_stack2c(void* addr)	\
{ y *p = (y*)addr, v; try_tlb(addr); v = *p; klee_enable_softmmu(); return v; }

#define MMU_STORE(x,y)				\
void mmu_store_##x##_stack2c(void* addr, y v)	\
{	y *p = (y*)addr;			\
	if (!try_tlb(addr)) {			\
	void *sp = (void*)GET_STACK(kmc_regs_get());	\
	if (	sizeof(y) == 8 && sp == addr &&	\
		klee_stack_depth() < MAX_STACK_DEPTH) {		\
		ret_addr_locs[klee_stack_depth()] = sp;		\
	} else {						\
		chk_touch_prior_ptr(addr, x/8, v);		\
	}}							\
	*p = v;				\
	klee_enable_softmmu();	}


MMU_ACCESS_ALL();
DECL_MMUOPS_S(stack2c);
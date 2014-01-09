#include "klee/klee.h"
#include "mmu.h"

static uint64_t stack_begin = 0, stack_end = 0;

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

	/* find everything that looks like an address in the range */
	len = (uintptr_t)stk_bottom - (uintptr_t)stk_top;
	for (i = 0; i < len; i++) {
		uint64_t	*cur_addr;
		uint64_t	v;

		cur_addr = (uint64_t*)((char*)stk_top + i);
		v = *cur_addr;
		if (	!((v >= 0x400000 && v <= 0x600000) ||
			(v >= 0x7f0000000000 && v <= 0x800000000000)))
		{
			continue;
		}

		if (klee_fork_eq(addr, cur_addr))
			return cur_addr;
	}

	return NULL;
}

#define MMU_LOAD(x,y)			\
y mmu_load_##x##_stack(void* addr)	\
{ return mmu_load_##x##_objwide (addr); }

#define IN_STACK(x)	((x) >= stack_begin && (x) <= stack_end)

#define MMU_STORE(x,y)				\
void mmu_store_##x##_stack(void* addr, y v)	\
{	void* target_addr;			\
	stack_load();			\
	if (!IN_STACK((uint64_t)addr)) {		\
		mmu_store_##x##_objwide (addr, v);	\
		return;	\
	}	\
	if ((target_addr = find_ret_addr(addr))) {	\
		klee_assume_eq(target_addr, addr);	\
		*((y*)target_addr) = v;	}		\
}

MMU_ACCESS_ALL();
DECL_MMUOPS_S(stack);
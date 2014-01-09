#include "klee/klee.h"
#include "mmu_testptr.h"
#include "mmu.h"

#define PAGE_SZ		0x1000
#define PAGE_MASK	~(0xfffULL)

/* NOTE: the edge case below is handled when processing obj1
 * [ [ obj1 ][ obj2]  ] = PAGE
 *       [^...]
 * in this case (p_64 < obj_base), a new state is forked fork every
 * straddling/misaligned concretization. This is because klee can't do symbolic
 * accesses across object state yet.
 */
#define MMU_ITER(y,found_op)	\
{	\
uint64_t obj_base = (uint64_t)klee_get_obj_prev((void*)c_64); \
do {	uint64_t obj_sz;						\
	if (obj_base < c_64) goto next;					\
	obj_sz = klee_get_obj_size((void*)obj_base);			\
	if (p_64 < obj_base) break;					\
	if ((p_64 + sizeof(y)) <= (obj_base+obj_sz)) { found_op; }	\
next:	obj_base = (uint64_t)klee_get_obj_next((void*)(obj_base+1));	\
} while (obj_base < (c_64 + PAGE_SZ) && obj_base);			\
/* fork straddled / misaligned */	\
c_64 = klee_fork_all(p_64);		\
}

#define MMU_FIND_PAGES	\
	uint64_t	p_64 = (uint64_t)addr, c_64;	\
	mmu_testptr(addr);				\
	c_64 = klee_fork_all(p_64 & PAGE_MASK);

#define MMU_LOAD(x,y)		\
y mmu_load_##x##_objwide(void* addr) {	\
	MMU_FIND_PAGES			\
	MMU_ITER(y, return klee_wide_load_##x((void*)obj_base, addr));	\
	return *((y*)c_64);		\
}

#define MMU_STORE(x,y)			\
void mmu_store_##x##_objwide(void* addr, y v) {	\
	MMU_FIND_PAGES				\
	MMU_ITER(y, klee_wide_store_##x((void*)obj_base, addr, v); return);	\
	*((y*)c_64) = v;	\
}

#undef MMU_ACCESS
#define MMU_ACCESS(x,y)	\
	extern y klee_wide_load_##x(void*, y*);		\
	extern void klee_wide_store_##x(void*, void*, y v);	\
	MMU_LOAD(x,y)	\
	MMU_STORE(x,y)

MMU_ACCESS_ALL();
DECL_MMUOPS_S(objwide);
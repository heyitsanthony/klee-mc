#include "klee/klee.h"
#include "mmu_testptr.h"
#include "mmu.h"

MMUOPS_S_EXTERN(mayhem);

/**
 * the concretization priority algorithm, taken from MAYHEM
 */
static void* fork_on_symbolic(void* in_ptr)
{
	char		*cptr_min, *cptr_max;
	unsigned 	i, diff;
	
	cptr_min = (void*)klee_min_value((uint64_t)in_ptr);
	cptr_max = (void*)klee_max_value((uint64_t)in_ptr);

	diff = (unsigned long)(cptr_max - cptr_min);
	for (i = 0; i < diff; i++) {
		char	*p = &cptr_min[i];
		if (klee_is_symbolic(*p) && klee_prefer_eq(p, in_ptr)) {
			klee_print_expr("[MMU] Mayhem on ptr", in_ptr);
			return p;
		}
	}

	return NULL;
}

/* concretize pointers on symbolic data */
#define MMU_LOAD(x,y)		\
y mmu_load_##x##_mayhem(void* addr)	\
{	y		*p;		\
	mmu_testptr(addr);		\
	p = (y*)fork_on_symbolic(addr);	\
	if (p == NULL) return mmu_load_##x##_uniqptr(addr); \
	return *p; }

#define MMU_STORE(x,y)			\
void mmu_store_##x##_mayhem(void* addr, y v)	\
{ MMU_FWD_STORE(mayhem,x,addr,v); }

MMU_ACCESS_ALL();
DECL_MMUOPS_S(mayhem);
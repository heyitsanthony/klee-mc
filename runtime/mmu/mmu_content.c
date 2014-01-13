/* fork for every object *BUT* only access a single field */
/**
 * MMU which dispatches large reads with ITEs up to some maximum depth.
 */

#include "klee/klee.h"
#include "mmu.h"

MMUOPS_S_EXTERN(content);

#define MMU_LOAD(x,y)		\
y mmu_load_##x##_content(void* addr)	\
{	y		ret;		\
	uint64_t	pred = 1;	\
	int		fork_c = 0;	\
	while (1) {			\
		uint64_t	c;	\
		if (!__klee_feasible(pred)) break;		\
		c = klee_get_value_pred((uint64_t)addr, pred); 	\
		pred = klee_mk_and(pred, klee_mk_ne(addr, c)); 	\
		ret = *((y*)c);					\
		if (!klee_is_symbolic(ret)) continue;		\
		fork_c++;					\
		if (addr == (void*)c) return ret; }		\
	if (fork_c) klee_silent_exit(0);			\
	return MMU_FWD_LOAD(content,x,addr); }

#define MMU_STORE(x,y)			\
void mmu_store_##x##_content(void* addr, y v)	\
{ MMU_FWD_STORE(content,x,addr,v); }

MMU_ACCESS_ALL();
DECL_MMUOPS_S(content);

/**
 * MMU which dispatches writes with a special symbolic write log.
 *
 */

#include "klee/klee.h"
#include "mmu_testptr.h"
#include "mmu.h"

#if 0


struct sym_write
{
	uint64_t		sw_min;
	uint64_t		sw_max;
	uint8_t			sw_v;		/* XXX?? byte accesses */
	char			*sw_addr;
	int			sw_sz;
	struct sym_write	*sw_next;
};

struct sym_write	*sw_head = NULL;

void write_scrubber(void)
{

}

static void* find_matched_writes(
	struct sym_write* sw_start,
	void* addr,
	int sz)
{
	return NULL;
}

static struct sym_write* find_partial_match(
	struct sym_write* sw_start,
	void* addr,
	int sz)
{
	struct sym_write	*cur;
	char			*addr_c = addr;

	for (cur = sw_head; cur != NULL; cur = cur->sw_next) {
		if (	addr_c+sz >= cur->sw_addr && 
			addr_c < cur->sw_addr+cur->sw_sz)
		{
			/*    a----a+s
			 * b----b+r
			 */
			return cur;
		} else if (addr_c <= cur->sw_addr && addr_c+sz > cur->sw_addr) {
			/* a----a+s
			 *    b----b+r
			 */
			return cur;
		}
	}
	
	/* XXX */
	return NULL;
}


#define MMU_LOAD(x,y)		\
y mmu_load_##x##_symlog(void* addr) { return mmu_load_##x##_objwide(addr); }				

#define MMU_STORE(x,y)			\
void mmu_store_##x##_symlog(void* addr, y v)	\
{ mmu_store_##x##_objwide(addr,v); }

MMU_ACCESS_ALL();
DECL_MMUOPS_S(symlog);

#endif
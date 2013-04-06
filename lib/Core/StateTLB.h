#ifndef STATETLB_H
#define STATETLB_H

#include "Memory.h" 
#include "AddressSpace.h"

#define STLB_OBJCACHE_ENTS	128
#define STLB_PAGE_SZ		4096

#define STLB_ADDR2IDX(x)	((((x) / (STLB_PAGE_SZ*STLB_OBJCACHE_ENTS)) \
			^ ((x) / STLB_PAGE_SZ)) % STLB_OBJCACHE_ENTS)

namespace klee
{
class ExecutionState;

class StateTLB
{
public:
	StateTLB(void);
	StateTLB(const StateTLB& stlb);
	~StateTLB(void);
	bool get(ExecutionState& es, uint64_t addr, ObjectPair& out_op);
	void put(ExecutionState& es, ObjectPair& op);
	void invalidate(uint64_t addr);
private:
	void updateGen(ExecutionState& es);
	void initObjCache(void);
	void clearObjects(void);
	void clearCache(void);

	ObjectPair	*obj_cache;

	/* the two-gen split is a goofy way of avoiding
	 * misses when a new object state is allocated on write for
	 * a COW page */

	/* on increment, clear entire cache */
	unsigned	cur_gen_mo;

	/* on increment, clear cached object state pointers */
	unsigned	cur_gen_os;
};
}

#endif
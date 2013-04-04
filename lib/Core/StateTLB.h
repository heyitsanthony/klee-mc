#ifndef STATETLB_H
#define STATETLB_H

#include "Memory.h" 
#include "AddressSpace.h"
#include <string.h>

#define STLB_OBJCACHE_ENTS	128
#define STLB_PAGE_SZ		4096

#define STLB_ADDR2IDX(x)	((((x) / (STLB_PAGE_SZ*STLB_OBJCACHE_ENTS)) \
			^ ((x) / STLB_PAGE_SZ)) % STLB_OBJCACHE_ENTS)

namespace klee
{
class StateTLB
{
public:
	/* this is embedded in every state, so we only
	 * want to use the memory when it is needed */
	StateTLB(void) : obj_cache(0), cur_gen(~0) {}

	StateTLB(const StateTLB& stlb)
	: obj_cache(0)
	, cur_gen(~0)
	{
		if (stlb.obj_cache == NULL || this == &stlb)
			return;

		obj_cache = new ObjectPair[STLB_OBJCACHE_ENTS];
		cur_gen = stlb.cur_gen;
		memcpy(	obj_cache,
			stlb.obj_cache,
			sizeof(ObjectPair)*STLB_OBJCACHE_ENTS);
	}

	~StateTLB(void) { if (obj_cache) delete [] obj_cache; }
	bool get(unsigned gen, uint64_t addr, ObjectPair& out_op)
	{
		ObjectPair	*op;

		if (obj_cache == NULL)
			return false;

		if (gen != cur_gen)
			return false;

		op = &obj_cache[STLB_ADDR2IDX(addr)];
		if (op->first == NULL)
			return false;

		/* full boundary checks are done in the MMU. */
		if (op->first->isInBounds(addr, 1) == false)
			return false;

		out_op = *op;
		return true;
	}

	void put(unsigned gen, ObjectPair& op)
	{	if (obj_cache == NULL) initObjCache();
		if (gen != cur_gen) {
			clearCache();
			cur_gen = gen;
		}
		obj_cache[STLB_ADDR2IDX(op.first->address)] = op; }

	void invalidate(uint64_t addr)
	{	if (obj_cache == NULL) return;
		obj_cache[STLB_ADDR2IDX(addr)].first = NULL; }

private:
	void initObjCache(void)
	{ obj_cache = new ObjectPair[STLB_OBJCACHE_ENTS];
	  clearCache(); }

	void clearCache(void)
	{ memset(obj_cache, 0, sizeof(ObjectPair) * STLB_OBJCACHE_ENTS); }

	ObjectPair	*obj_cache;
	unsigned	cur_gen;
};
}

#endif
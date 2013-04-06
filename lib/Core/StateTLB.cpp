#include "StateTLB.h"
#include "klee/ExecutionState.h"
#include <string.h>

using namespace klee;

/* this is embedded in every state, so we only
 * want to use the memory when it is needed */
StateTLB::StateTLB(void)
: obj_cache(0)
, cur_gen_mo(~0)
, cur_gen_os(~0) {}

StateTLB::StateTLB(const StateTLB& stlb)
: obj_cache(0)
, cur_gen_mo(~0)
, cur_gen_os(~0)
{
	if (stlb.obj_cache == NULL || this == &stlb)
		return;

	obj_cache = new ObjectPair[STLB_OBJCACHE_ENTS];
	cur_gen_mo = stlb.cur_gen_mo;
	cur_gen_os = stlb.cur_gen_os;
	memcpy(	obj_cache,
		stlb.obj_cache,
		sizeof(ObjectPair)*STLB_OBJCACHE_ENTS);
}

StateTLB::~StateTLB(void) { if (obj_cache) delete [] obj_cache; }

bool StateTLB::get(ExecutionState& es, uint64_t addr, ObjectPair& out_op)
{
	ObjectPair	*op;

	if (obj_cache == NULL)
		return false;

	updateGen(es);

	op = &obj_cache[STLB_ADDR2IDX(addr)];
	if (op->first == NULL)
		return false;

	/* full boundary checks are done in the MMU. */
	if (op->first->isInBounds(addr, 1) == false)
		return false;

	out_op = *op;
	return true;
}

void StateTLB::put(ExecutionState& es, ObjectPair& op)
{	
	if (obj_cache == NULL)
		initObjCache();

	updateGen(es);
	obj_cache[STLB_ADDR2IDX(op.first->address)] = op;
}

void StateTLB::invalidate(uint64_t addr)
{
	if (obj_cache == NULL) return;
	obj_cache[STLB_ADDR2IDX(addr)].first = NULL;
}

void StateTLB::initObjCache(void)
{
	obj_cache = new ObjectPair[STLB_OBJCACHE_ENTS];
	clearCache();
}

void StateTLB::clearObjects(void)
{
	for (unsigned i = 0; i < STLB_OBJCACHE_ENTS; i++)
		obj_cache[i].second = NULL;
}

void StateTLB::clearCache(void)
{ memset(obj_cache, 0, sizeof(ObjectPair) * STLB_OBJCACHE_ENTS); }

void StateTLB::updateGen(ExecutionState& es)
{
	unsigned gen_os, gen_mo;

	gen_os = es.addressSpace.getGeneration();
	if (gen_os != cur_gen_os) {
		clearObjects();
		cur_gen_os = gen_os;
	}

	gen_mo = es.addressSpace.getGenerationMO();
	if (gen_mo != cur_gen_mo) {
		clearCache();
		cur_gen_mo = gen_mo;
	}
}

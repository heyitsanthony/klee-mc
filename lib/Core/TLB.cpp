#include <string.h>
#include "klee/ExecutionState.h"
#include "Memory.h"
#include "TLB.h"

using namespace klee;

TLB::TLB(void) : cur_state(0) {}

bool TLB::get(ExecutionState& st, uint64_t addr, ObjectPair& out_op)
{
	ObjectPair	*op;

	useState(&st);

	op = &obj_cache[(addr / 4096) % OBJCACHE_ENTS];
	if (op->first == NULL)
		return false;

	/* full boundary checks are done in the MMU. */
	if (op->first->isInBounds(addr, 1) == false)
		return false;

	out_op = *op;
	return true;
}

void TLB::put(ExecutionState& st, ObjectPair& op)
{
	useState(&st);
	obj_cache[(op.first->address / 4096) % OBJCACHE_ENTS] = op;
}

/* if address space changed, reset TLB */
void TLB::useState(const ExecutionState* st)
{
	if (st == cur_state && cur_gen == st->addressSpace.getGeneration())
		return;

	memset(&obj_cache, 0, sizeof(obj_cache));
	cur_state = st;
	cur_gen = st->addressSpace.getGeneration();
}

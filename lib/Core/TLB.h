#ifndef TLB_H
#define TLB_H

#include "AddressSpace.h"

#define OBJCACHE_ENTS	32

namespace klee
{
class ExecutionState;

class TLB
{
public:
	TLB(void);
	virtual ~TLB(void) {}
	bool get(ExecutionState& st, uint64_t addr, ObjectPair& op);
	void put(ExecutionState& st, ObjectPair& op);
private:
	void useState(const ExecutionState* st);
	const ExecutionState	*cur_state;	/* never deference this */
	unsigned		cur_gen;
	ObjectPair	obj_cache[OBJCACHE_ENTS];

};
}

#endif

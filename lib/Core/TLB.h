#ifndef TLB_H
#define TLB_H

#include "AddressSpace.h"

#define TLB_OBJCACHE_ENTS	256
#define TLB_PAGE_SZ		4096

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
	void invalidate(uint64_t addr);
private:
	void useState(const ExecutionState* st);
	uint64_t	cur_sid;
	unsigned	cur_gen;
	ObjectPair	obj_cache[TLB_OBJCACHE_ENTS];

};
}

#endif

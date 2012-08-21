#ifndef TAINTGROUP_H
#define TAINTGROUP_H

#include <map>
#include "../Expr/ShadowAlloc.h"

namespace klee
{
class ExecutionState;
class ShadowObjectState;


typedef std::pair<ref<Expr>, ShadowVal> TaintPair;

class TaintGroup
{
public:
	TaintGroup();
	virtual ~TaintGroup(void);
	void apply(ExecutionState *st, ShadowVal v);
	void addState(const ExecutionState *es);
	uint64_t getInsts(void) const { return total_insts; }
	void dump(void) const;
private:
	ref<Expr> getUntainted(ref<Expr> e);
	void addTaintedObject(uint64_t addr, const ShadowObjectState* sos);
	void mergeAddr(ExecutionState* st, uint64_t addr, ShadowVal v);
	bool mergeAddrIndep(
		uint64_t taint_addr,
		unsigned off,
		ShadowObjectState* sos);
	/* XXX: FUCK! how can we have symbolic taint when we're using 
	 * concrete addresses?? brlgrhghr */
	std::map<uint64_t, unsigned>	taint_addr_c;
	std::map<uint64_t, ref<Expr> >	taint_addr_v;
	std::map<uint64_t, std::set<TaintPair> >	taint_addr_all;

	unsigned		total_states;
	uint64_t		total_insts;
	unsigned		indep_byte_c, dep_byte_c, full_c;
	std::set<ref<Expr> >	untainted;
};
}
#endif

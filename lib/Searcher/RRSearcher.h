#ifndef RRSEARCHER_H
#define RRSEARCHER_H

#include "../Core/Searcher.h"

namespace klee
{
class RRSearcher : public Searcher
{
	std::list<ExecutionState*>		states;
	std::list<ExecutionState*>::iterator cur_state;
public:
	virtual Searcher* createEmpty(void) const
	{ return new RRSearcher(); }

	ExecutionState* selectState(bool allowCompact);
	RRSearcher() : cur_state(states.end()) {}
	virtual ~RRSearcher() {}

	void update(ExecutionState *current, const States s);
	bool empty() const { return states.empty(); }
	void printName(std::ostream &os) const { os << "RRSearcher\n"; }
};
}


#endif

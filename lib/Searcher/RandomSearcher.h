#ifndef RANDOMSEARCHER_H
#define RANDOMSEACHER_H

#include "../Core/Searcher.h"

namespace klee
{
class RandomSearcher : public Searcher
{
	std::vector<ExecutionState*> states;
	std::vector<ExecutionState*> statesNonCompact;

public:
	virtual ~RandomSearcher() {}
	virtual Searcher* createEmpty(void) const
	{ return new RandomSearcher(); }

	ExecutionState &selectState(bool allowCompact);
	void update(ExecutionState *current, const States s);
	bool empty() const { return states.empty(); }
	void printName(std::ostream &os) const { os << "RandomSearcher\n"; }
};
}

#endif

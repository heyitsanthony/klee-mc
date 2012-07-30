#ifndef ITERATIVEDEEPENINGTIMESEARCHER_H
#define ITERATIVEDEEPENINGTIMESEARCHER_H

#include "../Core/Searcher.h"

namespace klee
{
class IterativeDeepeningTimeSearcher : public Searcher
{
	Searcher *baseSearcher;
	double time, startTime;
	std::set<ExecutionState*> pausedStates;

public:
	IterativeDeepeningTimeSearcher(Searcher *baseSearcher);
	virtual ~IterativeDeepeningTimeSearcher();
	virtual Searcher* createEmpty(void) const
	{ return new IterativeDeepeningTimeSearcher(
		baseSearcher->createEmpty()); }

	ExecutionState &selectState(bool allowCompact);
	void update(ExecutionState *current, const States s);
	bool empty() const
	{ return baseSearcher->empty() && pausedStates.empty(); }

	void printName(std::ostream &os) const
	{
		os << "IterativeDeepeningTimeSearcher\n";
	}
};
}

#endif

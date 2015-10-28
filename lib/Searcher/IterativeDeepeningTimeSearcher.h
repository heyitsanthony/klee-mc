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

	Searcher* createEmpty(void) const override
	{ return new IterativeDeepeningTimeSearcher(
		baseSearcher->createEmpty()); }

	ExecutionState *selectState(bool allowCompact) override;
	void update(ExecutionState *current, const States s) override;
	void printName(std::ostream &os) const override {
		os << "IterativeDeepeningTimeSearcher\n";
	}
};
}

#endif

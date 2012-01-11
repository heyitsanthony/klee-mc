#ifndef BATCHINGSEARCHER_H
#define BATCHINGSEARCHER_H

#include "Searcher.h"

namespace klee
{
class BatchingSearcher : public Searcher
{
	Searcher *baseSearcher;
	double timeBudget;
	unsigned instructionBudget;

	ExecutionState *lastState;
	double lastStartTime;
	uint64_t lastStartInstructions;

	std::set<ExecutionState*> addedStates;
	std::set<ExecutionState*> removedStates;

public:
	BatchingSearcher(Searcher *baseSearcher, 
		     double _timeBudget,
		     unsigned _instructionBudget);
	virtual ~BatchingSearcher();

	ExecutionState &selectState(bool allowCompact);
	void update(ExecutionState *current, States s);
	bool empty() const { return baseSearcher->empty(); }

	void printName(std::ostream &os) const {
		os << "<BatchingSearcher> timeBudget: " << timeBudget
		 << ", instructionBudget: " << instructionBudget
		 << ", baseSearcher:\n";
		baseSearcher->printName(os);
		os << "</BatchingSearcher>\n";
	}

private:
	States getStates(void) const
	{ return States(addedStates, removedStates); }

	void clearStates(void)
	{
		addedStates.clear();
		removedStates.clear();
	}

	uint64_t getElapsedInstructions(void) const;
	double getElapsedTime(void) const;
};
}

#endif

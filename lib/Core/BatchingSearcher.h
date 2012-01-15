#ifndef BATCHINGSEARCHER_H
#define BATCHINGSEARCHER_H

#include "Searcher.h"

namespace klee
{
class BatchingSearcher : public Searcher
{
	Searcher		*baseSearcher;
	double			timeBudget;
	unsigned		instructionBudget;

	ExecutionState		*lastState;
	double			lastStartTime;
	uint64_t		lastStartInstructions;
	bool			select_new_state;

	std::set<ExecutionState*> addedStates;
	// We used to cache removed states here and then
	// flush them out on a select new state event. This is wrong.
	//
	// Once a removed state goes though the searcher update function, it
	// should be considered deleted for good and all pointers invalid.
	// std::set<ExecutionState*> removedStates;

public:
	BatchingSearcher(Searcher *baseSearcher,
		     double _timeBudget,
		     unsigned _instructionBudget);
	virtual ~BatchingSearcher();

	ExecutionState &selectState(bool allowCompact);
	void update(ExecutionState *current, States s);
	bool empty() const;

	void printName(std::ostream &os) const {
		os << "<BatchingSearcher> timeBudget: "
		<< timeBudget
		<< ", instructionBudget: " << instructionBudget
		<< ", baseSearcher:\n";

		baseSearcher->printName(os);
		os << "</BatchingSearcher>\n";
	}

private:
	uint64_t getElapsedInstructions(void) const;
	double getElapsedTime(void) const;
};
}

#endif

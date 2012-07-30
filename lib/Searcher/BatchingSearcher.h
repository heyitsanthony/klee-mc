#ifndef BATCHINGSEARCHER_H
#define BATCHINGSEARCHER_H

#include "../Core/Searcher.h"

namespace klee
{
class BatchingSearcher : public Searcher
{
public:
	BatchingSearcher(Searcher *baseSearcher,
		     double _timeBudget,
		     unsigned _instructionBudget);
	BatchingSearcher(Searcher *baseSearcher);

	virtual ~BatchingSearcher();

	virtual Searcher* createEmpty(void) const
	{ return new BatchingSearcher(
		baseSearcher->createEmpty(),
		timeBudget,
		instructionBudget); }


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
	uint64_t getElapsedQueries(void) const;
	double getElapsedTime(void) const;
	void handleTimeout(void);
	void adjustAdaptiveTime(void);

	Searcher		*baseSearcher;
	double			timeBudget;
	double			timeBudget_base;
	unsigned		instructionBudget;

	ExecutionState		*lastState;
	double			lastStartTime;
	uint64_t		lastStartInstructions;
	uint64_t		lastStartCov;
	uint64_t		lastStartQueries;
	bool			select_new_state;

	std::set<ExecutionState*> addedStates;
	// We used to cache removed states here and then
	// flush them out on a select new state event. This is wrong.
	//
	// Once a removed state goes though the searcher update function, it
	// should be considered deleted for good and all pointers invalid.
	// std::set<ExecutionState*> removedStates;
};
}

#endif

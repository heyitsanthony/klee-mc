#include "Executor.h"
#include "klee/Internal/System/Time.h"
#include "CoreStats.h"

#include "BatchingSearcher.h"

using namespace klee;

BatchingSearcher::BatchingSearcher(
	Searcher *_baseSearcher,
        double _timeBudget,
        unsigned _instructionBudget)
: baseSearcher(_baseSearcher)
, timeBudget(_timeBudget)
, instructionBudget(_instructionBudget)
, lastState(0)
, lastStartInstructions(0)
{}

BatchingSearcher::~BatchingSearcher() { delete baseSearcher; }


uint64_t BatchingSearcher::getElapsedInstructions(void) const
{
	return (stats::instructions - lastStartInstructions);
}

double BatchingSearcher::getElapsedTime(void) const
{
	return util::estWallTime() - lastStartTime;
}

ExecutionState &BatchingSearcher::selectState(bool allowCompact)
{
	if (	lastState && 
		(timeBudget < 0.0 || getElapsedTime() <= timeBudget) &&
		getElapsedInstructions() <= instructionBudget)
	{
		return *lastState;
	}

	if (lastState && timeBudget >= 0.0) {
		double delta = getElapsedTime();
		if (delta > timeBudget * 1.5) {
			std::cerr << "KLEE: increased time budget from "
				<< timeBudget << " to " << delta << "\n";
			timeBudget = delta;
		}
	}

	baseSearcher->update(lastState, getStates());
	clearStates();

	lastState = &baseSearcher->selectState(allowCompact);
	lastStartTime = util::estWallTime();
	lastStartInstructions = stats::instructions;

	return *lastState;
}

template<typename C1, typename C2>
bool is_disjoint(const C1& a, const C2& b)
{
  typename C1::const_iterator i = a.begin(), ii = a.end();
  typename C2::const_iterator j = b.begin(), jj = b.end();

  while (i != ii && j != jj) {
    if (*i < *j) ++i;
    else if (*j < *i) ++j;
    else return false;
  }

  return true;
}

void BatchingSearcher::update(ExecutionState *current, const States s)
{
	assert(is_disjoint(s.getAdded(), s.getRemoved()));

	// If there are pending additions before removals, 
	// or pending removals before additions,
	// process the pending states first, 
	// since those may actually be different states!
	if (	!is_disjoint(addedStates, s.getRemoved()) ||
		!is_disjoint(removedStates, s.getAdded()))
	{
		baseSearcher->update(current, getStates());
		clearStates();
	}

	addedStates.insert(s.getAdded().begin(), s.getAdded().end());
	removedStates.insert(s.getRemoved().begin(), s.getRemoved().end());
	ignoreStates.insert(s.getIgnored().begin(), s.getIgnored().end());
	unignoreStates.insert(s.getUnignored().begin(), s.getUnignored().end());

	if (	lastState &&
		!s.getRemoved().count(lastState) &&
		!s.getIgnored().count(lastState))
	{
		return;
	}

	lastState = NULL;
	baseSearcher->update(current, getStates());
	clearStates();
}

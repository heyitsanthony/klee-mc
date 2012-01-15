#include "Executor.h"
#include "klee/Internal/System/Time.h"
#include "CoreStats.h"
#include "static/Sugar.h"

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
, lastStartTime(util::estWallTime())
, lastStartInstructions(0)
, select_new_state(true)
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
	if (lastState != NULL && !select_new_state)
		return *lastState;

	if (baseSearcher->empty()) {
		baseSearcher->update(
			NULL, States(addedStates, States::emptySet));
		addedStates.clear();
	}

	lastState = &baseSearcher->selectState(allowCompact);
	lastStartTime = util::estWallTime();
	lastStartInstructions = stats::instructions;
	select_new_state = false;

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
	bool exceeded_time;
	// assert(is_disjoint(s.getAdded(), s.getRemoved()));

	exceeded_time = (timeBudget > 0.0 && getElapsedTime() > timeBudget);

	select_new_state |= exceeded_time;
	select_new_state |= (
		instructionBudget &&
		getElapsedInstructions() >= instructionBudget);

	if (exceeded_time) {
	//	double delta = getElapsedTime();
	//	if (delta > timeBudget * 1.5) {
	//		std::cerr << "KLEE: increased time budget from "
	//			<< timeBudget << " to " << delta << "\n";
	//		timeBudget = delta;
	//	}
		std::cerr << "TIMEOUT TRIGGERED\n";
	}

#if 0
	bool	add_rmv_conflict;
	// If there are pending additions before removals,
	// or pending removals before additions,
	// process the pending states first,
	// since those may actually be different states!
	add_rmv_conflict = (is_disjoint(addedStates, s.getRemoved()) == false);
	add_rmv_conflict |= (is_disjoint(removedStates, s.getAdded()) == false);

	if (add_rmv_conflict) {
		baseSearcher->update(current, getStates());
		clearStates();
	}

	removedStates.insert(s.getRemoved().begin(), s.getRemoved().end());
#endif

	if (select_new_state == false && s.getRemoved().empty()) {
		addedStates.insert(s.getAdded().begin(), s.getAdded().end());
		return;
	}

	if (s.getRemoved().empty() == false) {
		baseSearcher->update(
			current,
			States(addedStates, States::emptySet));
		addedStates.clear();
	}

	addedStates.insert(s.getAdded().begin(), s.getAdded().end());

	if (addedStates.size() || s.getRemoved().size()) {
		baseSearcher->update(current, States(addedStates, s.getRemoved()));
		addedStates.clear();
	}

	if (select_new_state || s.getRemoved().count(lastState))
		lastState = NULL;
}

bool BatchingSearcher::empty() const
{
	if (addedStates.size())
		return false;
	return baseSearcher->empty();
}

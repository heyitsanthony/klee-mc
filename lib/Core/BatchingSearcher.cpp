#include "Executor.h"
#include "klee/Internal/System/Time.h"
#include "CoreStats.h"

#include "BatchingSearcher.h"

using namespace klee;

BatchingSearcher::BatchingSearcher(Searcher *_baseSearcher,
        double _timeBudget,
        unsigned _instructionBudget)
: baseSearcher(_baseSearcher),
timeBudget(_timeBudget),
instructionBudget(_instructionBudget),
lastState(0) {

}

BatchingSearcher::~BatchingSearcher() { delete baseSearcher; }

ExecutionState &BatchingSearcher::selectState(bool allowCompact)
{
  if (lastState && 
      (util::estWallTime() - lastStartTime) <= timeBudget &&
      (stats::instructions - lastStartInstructions) <= instructionBudget)
      return *lastState;

  if (lastState) {
    double delta = util::estWallTime() - lastStartTime;
    if (delta > timeBudget * 1.1) {
      std::cerr << "KLEE: increased time budget from " << timeBudget << " to " << delta << "\n";
      timeBudget = delta;
    }
  }

  baseSearcher->update(lastState, addedStates, removedStates, ignoreStates, unignoreStates);
  addedStates.clear();
  removedStates.clear();
  ignoreStates.clear();
  unignoreStates.clear();

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

void BatchingSearcher::update(ExecutionState *current,
        const ExeStateSet &addedStates_,
        const ExeStateSet &removedStates_,
        const ExeStateSet &ignoreStates_,
        const ExeStateSet &unignoreStates_)
{
  assert(is_disjoint(addedStates_, removedStates_));

  // If there are pending additions before removals, or pending removals before additions,
  // process the pending states first, since those may actually be different states!
  if (!is_disjoint(addedStates, removedStates_) ||
      !is_disjoint(removedStates, addedStates_))
  {
    baseSearcher->update(current, addedStates, removedStates, ignoreStates, unignoreStates);
    addedStates.clear();
    removedStates.clear();
    ignoreStates.clear();
    unignoreStates.clear();
  }

  addedStates.insert(addedStates_.begin(), addedStates_.end());
  removedStates.insert(removedStates_.begin(), removedStates_.end());
  ignoreStates.insert(ignoreStates_.begin(), ignoreStates_.end());
  unignoreStates.insert(unignoreStates_.begin(), unignoreStates_.end());

  if (!lastState || removedStates_.count(lastState) || ignoreStates_.count(lastState)) {
    lastState = 0;
    baseSearcher->update(current, addedStates, removedStates, ignoreStates, unignoreStates);
    addedStates.clear();
    removedStates.clear();
    ignoreStates.clear();
    unignoreStates.clear();
  }
}

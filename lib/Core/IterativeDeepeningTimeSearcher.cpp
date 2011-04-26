#include "klee/Internal/System/Time.h"
#include "Executor.h"
#include "Sugar.h"

#include "IterativeDeepeningTimeSearcher.h"

using namespace klee;

IterativeDeepeningTimeSearcher::IterativeDeepeningTimeSearcher(Searcher *_baseSearcher)
: baseSearcher(_baseSearcher),
  time(1.) 
{
}

IterativeDeepeningTimeSearcher::~IterativeDeepeningTimeSearcher()
{
  delete baseSearcher;
}

ExecutionState &IterativeDeepeningTimeSearcher::selectState(bool allowCompact) {
  ExecutionState &res = baseSearcher->selectState(allowCompact);
  startTime = util::estWallTime();
  return res;
}

void IterativeDeepeningTimeSearcher::update(
  ExecutionState *current,
  const ExeStateSet &addedStates,
  const ExeStateSet &removedStates,
  const ExeStateSet &ignoreStates,
  const ExeStateSet &unignoreStates)
{
  double elapsed = util::estWallTime() - startTime;

  if (!removedStates.empty()) {
    std::set<ExecutionState *> alt = removedStates;
    foreach(it, removedStates.begin(), removedStates.end())
    {
      ExecutionState *es = *it;
      ExeStateSet::const_iterator p_it = pausedStates.find(es);
      if (p_it != pausedStates.end()) {
        pausedStates.erase(p_it);
        alt.erase(alt.find(es));
      }
    }
    baseSearcher->update(current, addedStates, alt, ignoreStates, unignoreStates);
  } else {
    baseSearcher->update(current, addedStates, removedStates, ignoreStates, unignoreStates);
  }

  if (current && !removedStates.count(current) && elapsed > time) {
    pausedStates.insert(current);
    baseSearcher->removeState(current);
  }

  if (baseSearcher->empty()) {
    time *= 2;
    std::cerr << "KLEE: increasing time budget to: " << time << "\n";
    baseSearcher->update(0, pausedStates, ExeStateSet(), ignoreStates, unignoreStates);
    pausedStates.clear();
  }
}

#ifndef ITERATIVEDEEPENINGTIMESEARCHER_H
#define ITERATIVEDEEPENINGTIMESEARCHER_H

#include "Searcher.h"

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

    ExecutionState &selectState(bool allowCompact);
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates,
                const std::set<ExecutionState*> &ignoreStates,
                const std::set<ExecutionState*> &unignoreStates);
    bool empty() const { return baseSearcher->empty() && pausedStates.empty(); }
    void printName(std::ostream &os) const {
      os << "IterativeDeepeningTimeSearcher\n";
    }
  };
}

#endif

#ifndef RANDOMPATHSEARCHER_H
#define RANDOMPATHSEARCHER_H

#include "Searcher.h"

namespace klee
{
  class RandomPathSearcher : public Searcher 
  {
    Executor &executor;
    
  public:
    RandomPathSearcher(Executor &_executor);
    virtual ~RandomPathSearcher() {}
    
    ExecutionState &selectState(bool allowCompact);
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates,
                const std::set<ExecutionState*> &ignoreStates,
                const std::set<ExecutionState*> &unignoreStates);
    bool empty() const;
    void printName(std::ostream &os) const { os << "RandomPathSearcher\n"; }
  };
}
#endif

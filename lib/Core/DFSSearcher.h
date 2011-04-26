#ifndef DFSSEARCHER_H
#define DFSSEARCHER_H

#include "Searcher.h"

namespace klee
{
  class DFSSearcher : public Searcher
  {
    std::list<ExecutionState*> states;

  public:
    ExecutionState &selectState(bool allowCompact);
    virtual ~DFSSearcher() {}

    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates,
                const std::set<ExecutionState*> &ignoreStates,
                const std::set<ExecutionState*> &unignoreStates);
    bool empty() const { return states.empty(); }
    void printName(std::ostream &os) const { os << "DFSSearcher\n"; }
  };
}


#endif

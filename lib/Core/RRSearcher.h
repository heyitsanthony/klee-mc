#ifndef RRSEARCHER_H
#define RRSEARCHER_H

#include "Searcher.h"

namespace klee
{
  class RRSearcher : public Searcher
  {
    std::list<ExecutionState*>		states;
    std::list<ExecutionState*>::iterator cur_state;
  public:
    ExecutionState &selectState(bool allowCompact);
    RRSearcher() : cur_state(states.end()) {}
    virtual ~RRSearcher() {}

    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates,
                const std::set<ExecutionState*> &ignoreStates,
                const std::set<ExecutionState*> &unignoreStates);
    bool empty() const { return states.empty(); }
    void printName(std::ostream &os) const { os << "RRSearcher\n"; }
  };
}


#endif

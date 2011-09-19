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

    void update(ExecutionState *current, States s);
    bool empty() const { return states.empty(); }
    void printName(std::ostream &os) const { os << "DFSSearcher\n"; }
  };
}


#endif

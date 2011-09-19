#ifndef BFSSEARCHER_H
#define BFSSEARCHER_H

#include <deque>
#include "Searcher.h"

namespace klee
{
  class BFSSearcher : public Searcher
  {
    std::deque<ExecutionState*> states;

  public:
    ExecutionState &selectState(bool allowCompact);
    virtual ~BFSSearcher() {}

    void update(ExecutionState *current, States s);
    bool empty() const { return states.empty(); }
    void printName(std::ostream &os) const { os << "BFSSearcher\n"; }
  };
}


#endif

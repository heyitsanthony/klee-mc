#ifndef MERGINGSEARCHER_H
#define MERGINGSEARCHER_H

#include "ExecutorBC.h"
#include "Searcher.h"

namespace klee
{
  class MergingSearcher : public Searcher
  {
    ExecutorBC &executor;
    std::set<ExecutionState*> statesAtMerge;
    Searcher *baseSearcher;
    llvm::Function *mergeFunction;

  private:
    llvm::Instruction *getMergePoint(ExecutionState &es);

  public:
    MergingSearcher(ExecutorBC &executor, Searcher *baseSearcher);
    virtual ~MergingSearcher();

    ExecutionState &selectState(bool allowCompact);
    void update(ExecutionState *current, const States s);
    bool empty() const { return baseSearcher->empty() && statesAtMerge.empty(); }
    void printName(std::ostream &os) const { os << "MergingSearcher\n"; }
  };
}

#endif

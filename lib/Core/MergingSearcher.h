#ifndef MERGINGSEARCHER_H
#define MERGINGSEARCHER_H

#include "Searcher.h"

namespace klee
{
  class MergingSearcher : public Searcher
  {
    Executor &executor;
    std::set<ExecutionState*> statesAtMerge;
    Searcher *baseSearcher;
    llvm::Function *mergeFunction;

  private:
    llvm::Instruction *getMergePoint(ExecutionState &es);

  public:
    MergingSearcher(Executor &executor, Searcher *baseSearcher);
    virtual ~MergingSearcher();

    ExecutionState &selectState(bool allowCompact);
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates,
                const std::set<ExecutionState*> &ignoreStates,
                const std::set<ExecutionState*> &unignoreStates);
    bool empty() const { return baseSearcher->empty() && statesAtMerge.empty(); }
    void printName(std::ostream &os) const { os << "MergingSearcher\n"; }
  };
}

#endif

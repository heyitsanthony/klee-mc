#ifndef BUMPINGMERGESEARCHER_H
#define BUMPINGMERGESEARCHER_H

#include "ExecutorBC.h"
#include "Searcher.h"

namespace klee
{
  class BumpMergingSearcher : public Searcher
  {
    ExecutorBC &executor;
    std::map<llvm::Instruction*, ExecutionState*> statesAtMerge;
    Searcher *baseSearcher;
    llvm::Function *mergeFunction;

  private:
    llvm::Instruction *getMergePoint(ExecutionState &es);

  public:
    BumpMergingSearcher(ExecutorBC &executor, Searcher *baseSearcher);
    virtual ~BumpMergingSearcher();

    ExecutionState &selectState(bool allowCompact);
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates,
                const std::set<ExecutionState*> &removedStates,
                const std::set<ExecutionState*> &ignoreStates,
                const std::set<ExecutionState*> &unignoreStates);
    bool empty() const { return baseSearcher->empty() && statesAtMerge.empty(); }
    void printName(std::ostream &os) const { os << "BumpMergingSearcher\n"; }
  };
}

#endif

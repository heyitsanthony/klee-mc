#ifndef MERGINGSEARCHER_H
#define MERGINGSEARCHER_H

#include "../Core/ExecutorBC.h"
#include "../Core/Searcher.h"

namespace klee
{
  class MergingSearcher : public Searcher
  {
    ExecutorBC &executor;
    ExeStateSet statesAtMerge;
    Searcher *baseSearcher;
    llvm::Function *mergeFunction;

  private:
    llvm::Instruction *getMergePoint(ExecutionState &es);

  public:
    MergingSearcher(ExecutorBC &executor, Searcher *baseSearcher);
    virtual ~MergingSearcher();
    Searcher* createEmpty(void) const override
    { return new MergingSearcher(executor, baseSearcher->createEmpty()); }

    ExecutionState *selectState(bool allowCompact) override;
    void update(ExecutionState *current, const States s) override;
    void printName(std::ostream &os) const override { os << "MergingSearcher\n"; }
  };
}

#endif

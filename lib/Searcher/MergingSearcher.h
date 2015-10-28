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
    virtual Searcher* createEmpty(void) const
    { return new MergingSearcher(executor, baseSearcher->createEmpty()); }

    ExecutionState *selectState(bool allowCompact);
    void update(ExecutionState *current, const States s);
    bool empty() const { return baseSearcher->empty() && statesAtMerge.empty(); }
    void printName(std::ostream &os) const { os << "MergingSearcher\n"; }
  };
}

#endif

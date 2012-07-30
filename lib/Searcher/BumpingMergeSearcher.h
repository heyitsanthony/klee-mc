#ifndef BUMPINGMERGESEARCHER_H
#define BUMPINGMERGESEARCHER_H

#include "../Core/ExecutorBC.h"
#include "../Core/Searcher.h"

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
    virtual Searcher* createEmpty(void) const
   { return new BumpMergingSearcher(executor, baseSearcher->createEmpty()); }


    ExecutionState &selectState(bool allowCompact);
    void update(ExecutionState *current, States s);
    bool empty() const { return baseSearcher->empty() && statesAtMerge.empty(); }
    void printName(std::ostream &os) const { os << "BumpMergingSearcher\n"; }
  };
}

#endif

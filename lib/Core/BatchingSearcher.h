#ifndef BATCHINGSEARCHER_H
#define BATCHINGSEARCHER_H

#include "Searcher.h"

namespace klee
{
  class BatchingSearcher : public Searcher {
    Searcher *baseSearcher;
    double timeBudget;
    unsigned instructionBudget;

    ExecutionState *lastState;
    double lastStartTime;
    uint64_t lastStartInstructions;
    
    std::set<ExecutionState*> addedStates;
    std::set<ExecutionState*> removedStates;
    std::set<ExecutionState*> ignoreStates;
    std::set<ExecutionState*> unignoreStates;

  public:
    BatchingSearcher(Searcher *baseSearcher, 
                     double _timeBudget,
                     unsigned _instructionBudget);
    virtual ~BatchingSearcher();
    
    ExecutionState &selectState(bool allowCompact);
    void update(ExecutionState *current,
                const std::set<ExecutionState*> &addedStates_,
                const std::set<ExecutionState*> &removedStates_,
                const std::set<ExecutionState*> &ignoreStates_,
                const std::set<ExecutionState*> &unignoreStates_);
    bool empty() const { return baseSearcher->empty(); }
    void printName(std::ostream &os) const {
      os << "<BatchingSearcher> timeBudget: " << timeBudget
         << ", instructionBudget: " << instructionBudget
         << ", baseSearcher:\n";
      baseSearcher->printName(os);
      os << "</BatchingSearcher>\n";
    }

  private:
    uint64_t getElapsedInstructions(void) const;
    double getElapsedTime(void) const;
  };
}

#endif

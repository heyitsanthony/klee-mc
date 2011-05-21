//===-- Searcher.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Searcher.h"
#include "Executor.h"
#include "ExeStateManager.h"

#include "BumpingMergeSearcher.h"
#include "DFSSearcher.h"

#include "Sugar.h"

using namespace klee;
using namespace llvm;

ExecutionState &DFSSearcher::selectState(bool allowCompact)
{
  foreach (i, states.rbegin(), states.rend()) {
    ExecutionState* es = *i;
    if (!allowCompact && es->isCompactForm) continue;
    return *es;
  }
  // no non-compact [if !allowCompact]) states remain
  return *states.back();
}

void DFSSearcher::update(ExecutionState *current,
        const ExeStateSet &addedStates,
        const ExeStateSet &removedStates,
        const ExeStateSet &ignoreStates,
        const ExeStateSet &unignoreStates)
{
  states.insert(states.end(),
          addedStates.begin(),
          addedStates.end());

  if (removedStates.empty()) return;

  /* hack for common case of removing only one state...
   * no need to scan the entire state list */
  if (removedStates.count(states.back())) {
    states.pop_back();
    if (removedStates.size() == 1)
      return;
  }

  for (std::list<ExecutionState*>::iterator it = states.begin(),
    ie = states.end(); it != ie;)
  {
    ExecutionState* es = *it;
    if (removedStates.count(es)) {
      it = states.erase(it);
    } else {
      ++it;
    }
  }
}

///
///

BumpMergingSearcher::BumpMergingSearcher(Executor &_executor, Searcher *_baseSearcher)
: executor(_executor),
baseSearcher(_baseSearcher),
mergeFunction(executor.kmodule->kleeMergeFn) {
}

BumpMergingSearcher::~BumpMergingSearcher() {
  delete baseSearcher;
}

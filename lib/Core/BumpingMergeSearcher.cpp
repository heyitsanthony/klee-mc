#include "Executor.h"
#include "llvm/Instructions.h"
#include "llvm/Support/CallSite.h"
#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KModule.h"

#include "BumpingMergeSearcher.h"

using namespace llvm;
using namespace klee;

Instruction *BumpMergingSearcher::getMergePoint(ExecutionState &es)
{
  if (!mergeFunction) return 0;
  Instruction *i = es.pc->inst;

  if (i->getOpcode() == Instruction::Call) {
    CallSite cs(cast<CallInst > (i));
    if (mergeFunction == cs.getCalledFunction())
      return i;
  }

  return 0;
}

/* FIXME Backwards goto? Yeah, right. */
ExecutionState &BumpMergingSearcher::selectState(bool allowCompact)
{
entry:
  Instruction *mp;

  // out of base states, pick one to pop
  if (baseSearcher->empty()) {
    std::map<llvm::Instruction*, ExecutionState*>::iterator it =
            statesAtMerge.begin();
    ExecutionState *es = it->second;
    statesAtMerge.erase(it);
    ++es->pc;

    baseSearcher->addState(es);
  }

  ExecutionState &es = baseSearcher->selectState(allowCompact);
  mp = getMergePoint(es);
  if (!mp) return es;
  std::map<llvm::Instruction*, ExecutionState*>::iterator it =
          statesAtMerge.find(mp);

  baseSearcher->removeState(&es);

  if (it == statesAtMerge.end()) {
    statesAtMerge.insert(std::make_pair(mp, &es));
  } else {
    ExecutionState *mergeWith = it->second;
    if (mergeWith->merge(es)) {
      // hack, because we are terminating the state we need to let
      // the baseSearcher know about it again
      baseSearcher->addState(&es);
      executor.terminateState(es);
    } else {
      it->second = &es; // the bump
      ++mergeWith->pc;

      baseSearcher->addState(mergeWith);
    }
  }

  goto entry;
}

void BumpMergingSearcher::update(ExecutionState *current,
        const ExeStateSet &addedStates,
        const ExeStateSet &removedStates,
        const ExeStateSet &ignoreStates,
        const ExeStateSet &unignoreStates) {
  baseSearcher->update(current, addedStates, removedStates, ignoreStates, unignoreStates);
}



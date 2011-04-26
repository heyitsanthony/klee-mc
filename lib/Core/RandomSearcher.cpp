#include "klee/Internal/ADT/RNG.h"
#include "Executor.h"
#include "RandomSearcher.h"

using namespace klee;

namespace klee { extern RNG theRNG; }

ExecutionState &RandomSearcher::selectState(bool allowCompact) {
  std::vector<ExecutionState*>& statePool = allowCompact ? states
          : statesNonCompact;
  return *statePool[theRNG.getInt32() % statePool.size()];
}

void RandomSearcher::update(ExecutionState *current,
        const ExeStateSet &addedStates,
        const ExeStateSet &removedStates,
        const ExeStateSet &ignoreStates,
        const ExeStateSet &unignoreStates) {
  states.insert(states.end(),
          addedStates.begin(),
          addedStates.end());
  for (ExeStateSet::const_iterator it = addedStates.begin();
          it != addedStates.end(); ++it) {
    if (!(*it)->isCompactForm)
      statesNonCompact.push_back(*it);
  }
  for (ExeStateSet::const_iterator it = removedStates.begin(),
          ie = removedStates.end(); it != ie; ++it) {
    ExecutionState *es = *it;

    std::vector<ExecutionState*>::iterator it2 = std::find(states.begin(),
            states.end(), es);
    assert(it2 != states.end() && "invalid state removed");
    states.erase(it2);
    if (es->isCompactForm) {
      it2 = std::find(statesNonCompact.begin(), statesNonCompact.end(), es);
      assert(it2 != statesNonCompact.end() && "invalid state removed");
      statesNonCompact.erase(it2);
    }
  }
}



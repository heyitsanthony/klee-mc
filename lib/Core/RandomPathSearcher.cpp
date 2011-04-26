#include <fstream>
#include "klee/Internal/ADT/RNG.h"

#include "PTree.h"
#include "Executor.h"
#include "ExeStateManager.h"

#include "RandomPathSearcher.h"

using namespace klee;

namespace klee { extern RNG theRNG; }

RandomPathSearcher::RandomPathSearcher(Executor &_executor)
: executor(_executor) {
}

ExecutionState &RandomPathSearcher::selectState(bool allowCompact) {
  executor.processTree->checkRep();

  unsigned flips = 0, bits = 0;
  PTree::Node *n = executor.processTree->root;

  assert(!n->ignore && "no state selectable");

  while (!n->data) {
    if (!n->left
            || n->left->ignore
            || !n->sumLeft[PTree::WeightAndCompact]
            || (!allowCompact && !n->sumLeft[PTree::WeightAnd])) {
      n = n->right;
    } else if (!n->right
            || n->right->ignore
            || !n->sumRight[PTree::WeightAndCompact]
            || (!allowCompact && !n->sumRight[PTree::WeightAnd])) {
      n = n->left;
    } else {
      if (bits == 0) {
        flips = theRNG.getInt32();
        bits = 32;
      }
      --bits;
      assert(!n->left->ignore && !n->right->ignore);
      n = (flips & (1 << bits)) ? n->left : n->right;
    }

    if (!n) {
      std::ofstream os;
      std::string name = "process.dot";
      os.open(name.c_str());
      executor.processTree->dump(os);

      os.flush();
      os.close();
    }

    assert(n && "RandomPathSearcher hit unexpected dead end");

  }
  executor.processTree->checkRep();
  assert(!n->ignore);
  return *n->data;
}

void RandomPathSearcher::update(ExecutionState *current,
        const ExeStateSet &addedStates,
        const ExeStateSet &removedStates,
        const ExeStateSet &ignoreStates,
        const ExeStateSet &unignoreStates)
{
  for (ExeStateSet::const_iterator
    it = ignoreStates.begin(),
    ie = ignoreStates.end(); it != ie; ++it)
  {

    ExecutionState *state = *it;
    executor.processTree->checkRep();
    PTree::Node *n = state->ptreeNode;

    assert(!n->right && !n->left);
    while (n && (!n->right || n->right->ignore) && (!n->left || n->left->ignore)) {
      n->ignore = true;
      n = n->parent;
    }

    executor.processTree->checkRep();
  }

  for (ExeStateSet::const_iterator 
    it = unignoreStates.begin(),
    ie = unignoreStates.end(); it != ie; ++it)
  {
    ExecutionState *state = *it;
    executor.processTree->checkRep();
    PTree::Node *n = state->ptreeNode;

    while (n) {
      n->ignore = false;
      n = n->parent;
    }

    executor.processTree->checkRep();
  }
}

bool RandomPathSearcher::empty() const
{
  return executor.stateManager->empty();
}

//===-- Searcher.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Searcher.h"

#include "CoreStats.h"
#include "Executor.h"
#include "PTree.h"
#include "StatsTracker.h"

#include "klee/Common.h"
#include "klee/ExecutionState.h"
#include "klee/Statistics.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/ADT/DiscretePDF.h"
#include "klee/Internal/ADT/RNG.h"
#include "klee/Internal/Support/ModuleUtil.h"
#include "klee/Internal/System/Time.h"

#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/CommandLine.h"

#include <cassert>
#include <fstream>
#include <climits>

using namespace klee;
using namespace llvm;

namespace {
  cl::opt<bool>
  DebugLogMerge("debug-log-merge");
}

namespace klee {
  extern RNG theRNG;
}

Searcher::~Searcher() {
}

///

ExecutionState &DFSSearcher::selectState(bool allowCompact) {
  std::list<ExecutionState*>::reverse_iterator i;

  for (i = states.rbegin(); i != states.rend(); i++) {
    ExecutionState* es = *i;
    if (!allowCompact && es->isCompactForm) continue;
    return *es;
  }
  // no non-compact [if !allowCompact]) states remain
  return *states.back();
}

void DFSSearcher::update(ExecutionState *current,
        const std::set<ExecutionState*> &addedStates,
        const std::set<ExecutionState*> &removedStates,
        const std::set<ExecutionState*> &ignoreStates,
        const std::set<ExecutionState*> &unignoreStates) {
  states.insert(states.end(),
          addedStates.begin(),
          addedStates.end());

  if (!removedStates.empty()) {
    if (removedStates.count(states.back())) {
      states.pop_back();
      if (removedStates.size() == 1)
        return;
    }
    for (std::list<ExecutionState*>::iterator it = states.begin(),
            ie = states.end(); it != ie;) {
      ExecutionState* es = *it;
      if (removedStates.count(es)) {
        it = states.erase(it);
      } else {
        ++it;
      }
    }
  }

}

///

ExecutionState &RandomSearcher::selectState(bool allowCompact) {
  std::vector<ExecutionState*>& statePool = allowCompact ? states
          : statesNonCompact;
  return *statePool[theRNG.getInt32() % statePool.size()];
}

void RandomSearcher::update(ExecutionState *current,
        const std::set<ExecutionState*> &addedStates,
        const std::set<ExecutionState*> &removedStates,
        const std::set<ExecutionState*> &ignoreStates,
        const std::set<ExecutionState*> &unignoreStates) {
  states.insert(states.end(),
          addedStates.begin(),
          addedStates.end());
  for (std::set<ExecutionState*>::const_iterator it = addedStates.begin();
          it != addedStates.end(); ++it) {
    if (!(*it)->isCompactForm)
      statesNonCompact.push_back(*it);
  }
  for (std::set<ExecutionState*>::const_iterator it = removedStates.begin(),
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

///

WeightedRandomSearcher::WeightedRandomSearcher(Executor &_executor,
        WeightType _type)
: executor(_executor),
states(new DiscretePDF<ExecutionState*>()),
type(_type) {
  switch (type) {
    case Depth:
      updateWeights = false;
      break;
    case InstCount:
    case CPInstCount:
    case QueryCost:
    case MinDistToUncovered:
    case CoveringNew:
      updateWeights = true;
      break;
    default:
      assert(0 && "invalid weight type");
  }
}

WeightedRandomSearcher::~WeightedRandomSearcher() {
  delete states;
}

ExecutionState &WeightedRandomSearcher::selectState(bool allowCompact) {
  ExecutionState *es = states->choose(theRNG.getDoubleL(), allowCompact);
  return *es;
}

double WeightedRandomSearcher::getWeight(ExecutionState *es) {
  if (es->isCompactForm
          || es->replayBranchIterator != es->branchDecisionsSequence.end())
    return es->weight;

  switch (type) {
    default:
    case Depth:
      return es->weight;
    case InstCount:
    {
      uint64_t count = theStatisticManager->getIndexedValue(stats::instructions,
              es->pc->info->id);
      double inv = 1. / std::max((uint64_t) 1, count);
      return es->weight = inv * inv;
    }
    case CPInstCount:
    {
      StackFrame &sf = es->stack.back();
      uint64_t count = sf.callPathNode->statistics.getValue(stats::instructions);
      double inv = 1. / std::max((uint64_t) 1, count);
      return es->weight = inv;
    }
    case QueryCost:
      return es->weight = (es->queryCost < .1) ? 1. : 1. / es->queryCost;
    case CoveringNew:
    case MinDistToUncovered:
    {
      uint64_t md2u = computeMinDistToUncovered(es->pc,
              es->stack.back().minDistToUncoveredOnReturn);

      double invMD2U = 1. / (md2u ? md2u : 10000);
      if (type == CoveringNew) {
        double invCovNew = 0.;
        if (es->instsSinceCovNew)
          invCovNew = 1. / std::max(1, (int) es->instsSinceCovNew - 1000);
        return es->weight = (invCovNew * invCovNew + invMD2U * invMD2U);
      } else {
        return es->weight = invMD2U * invMD2U;
      }
    }
  }
}

void WeightedRandomSearcher::update(ExecutionState *current,
        const std::set<ExecutionState*> &addedStates,
        const std::set<ExecutionState*> &removedStates,
        const std::set<ExecutionState*> &ignoreStates,
        const std::set<ExecutionState*> &unignoreStates) {
  if (current && updateWeights && !removedStates.count(current) && !ignoreStates.count(current))
    states->update(current, getWeight(current));

  for (std::set<ExecutionState*>::const_iterator it = addedStates.begin(),
          ie = addedStates.end(); it != ie; ++it) {
    ExecutionState *es = *it;
    states->insert(es, getWeight(es), es->isCompactForm);
  }

  for (std::set<ExecutionState*>::const_iterator it = ignoreStates.begin(),
          ie = ignoreStates.end(); it != ie; ++it) {
    ExecutionState *es = *it;
    states->remove(es);
  }

  for (std::set<ExecutionState*>::const_iterator it = unignoreStates.begin(),
          ie = unignoreStates.end(); it != ie; ++it) {
    ExecutionState *es = *it;
    states->insert(es, getWeight(es), es->isCompactForm);
  }

  for (std::set<ExecutionState*>::const_iterator it = removedStates.begin(),
          ie = removedStates.end(); it != ie; ++it) {
    ExecutionState* es = *it;
    states->remove(es);
  }
}

bool WeightedRandomSearcher::empty() {
  return states->empty();
}

///

RandomPathSearcher::RandomPathSearcher(Executor &_executor)
: executor(_executor) {
}

RandomPathSearcher::~RandomPathSearcher() {
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
        const std::set<ExecutionState*> &addedStates,
        const std::set<ExecutionState*> &removedStates,
        const std::set<ExecutionState*> &ignoreStates,
        const std::set<ExecutionState*> &unignoreStates) {

  for (std::set<ExecutionState*>::const_iterator it = ignoreStates.begin(),
          ie = ignoreStates.end(); it != ie; ++it) {

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

  for (std::set<ExecutionState*>::const_iterator it = unignoreStates.begin(),
          ie = unignoreStates.end(); it != ie; ++it) {

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

bool RandomPathSearcher::empty() {
  return executor.states.empty();
}

///

BumpMergingSearcher::BumpMergingSearcher(Executor &_executor, Searcher *_baseSearcher)
: executor(_executor),
baseSearcher(_baseSearcher),
mergeFunction(executor.kmodule->kleeMergeFn) {
}

BumpMergingSearcher::~BumpMergingSearcher() {
  delete baseSearcher;
}

///

Instruction *BumpMergingSearcher::getMergePoint(ExecutionState &es) {
  if (mergeFunction) {
    Instruction *i = es.pc->inst;

    if (i->getOpcode() == Instruction::Call) {
      CallSite cs(cast<CallInst > (i));
      if (mergeFunction == cs.getCalledFunction())
        return i;
    }
  }

  return 0;
}

ExecutionState &BumpMergingSearcher::selectState(bool allowCompact) {
entry:
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

  if (Instruction * mp = getMergePoint(es)) {
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
  } else {
    return es;
  }
}

void BumpMergingSearcher::update(ExecutionState *current,
        const std::set<ExecutionState*> &addedStates,
        const std::set<ExecutionState*> &removedStates,
        const std::set<ExecutionState*> &ignoreStates,
        const std::set<ExecutionState*> &unignoreStates) {
  baseSearcher->update(current, addedStates, removedStates, ignoreStates, unignoreStates);
}

///

MergingSearcher::MergingSearcher(Executor &_executor, Searcher *_baseSearcher)
: executor(_executor),
baseSearcher(_baseSearcher),
mergeFunction(executor.kmodule->kleeMergeFn) {
}

MergingSearcher::~MergingSearcher() {
  delete baseSearcher;
}

///

Instruction *MergingSearcher::getMergePoint(ExecutionState &es) {
  if (mergeFunction) {
    Instruction *i = es.pc->inst;

    if (i->getOpcode() == Instruction::Call) {
      CallSite cs(cast<CallInst > (i));
      if (mergeFunction == cs.getCalledFunction())
        return i;
    }
  }

  return 0;
}

ExecutionState &MergingSearcher::selectState(bool allowCompact) {
  while (!baseSearcher->empty()) {
    ExecutionState &es = baseSearcher->selectState(allowCompact);
    if (getMergePoint(es)) {
      baseSearcher->removeState(&es, &es);
      statesAtMerge.insert(&es);
    } else {
      return es;
    }
  }

  // build map of merge point -> state list
  std::map<Instruction*, std::vector<ExecutionState*> > merges;
  for (std::set<ExecutionState*>::const_iterator it = statesAtMerge.begin(),
          ie = statesAtMerge.end(); it != ie; ++it) {
    ExecutionState &state = **it;
    Instruction *mp = getMergePoint(state);

    merges[mp].push_back(&state);
  }

  if (DebugLogMerge)
    std::cerr << "-- all at merge --\n";
  for (std::map<Instruction*, std::vector<ExecutionState*> >::iterator
    it = merges.begin(), ie = merges.end(); it != ie; ++it) {
    if (DebugLogMerge) {
      std::cerr << "\tmerge: " << it->first << " [";
      for (std::vector<ExecutionState*>::iterator it2 = it->second.begin(),
              ie2 = it->second.end(); it2 != ie2; ++it2) {
        ExecutionState *state = *it2;
        std::cerr << state << ", ";
      }
      std::cerr << "]\n";
    }

    // merge states
    std::set<ExecutionState*> toMerge(it->second.begin(), it->second.end());
    while (!toMerge.empty()) {
      ExecutionState *base = *toMerge.begin();
      toMerge.erase(toMerge.begin());

      std::set<ExecutionState*> toErase;
      for (std::set<ExecutionState*>::iterator it = toMerge.begin(),
              ie = toMerge.end(); it != ie; ++it) {
        ExecutionState *mergeWith = *it;

        if (base->merge(*mergeWith)) {
          toErase.insert(mergeWith);
        }
      }
      if (DebugLogMerge && !toErase.empty()) {
        std::cerr << "\t\tmerged: " << base << " with [";
        for (std::set<ExecutionState*>::iterator it = toErase.begin(),
                ie = toErase.end(); it != ie; ++it) {
          if (it != toErase.begin()) std::cerr << ", ";
          std::cerr << *it;
        }
        std::cerr << "]\n";
      }
      for (std::set<ExecutionState*>::iterator it = toErase.begin(),
              ie = toErase.end(); it != ie; ++it) {
        std::set<ExecutionState*>::iterator it2 = toMerge.find(*it);
        assert(it2 != toMerge.end());
        executor.terminateState(**it);
        toMerge.erase(it2);
      }

      // step past merge and toss base back in pool
      statesAtMerge.erase(statesAtMerge.find(base));
      ++base->pc;
      baseSearcher->addState(base);
    }
  }

  if (DebugLogMerge)
    std::cerr << "-- merge complete, continuing --\n";

  return selectState(allowCompact);
}

void MergingSearcher::update(ExecutionState *current,
        const std::set<ExecutionState*> &addedStates,
        const std::set<ExecutionState*> &removedStates,
        const std::set<ExecutionState*> &ignoreStates,
        const std::set<ExecutionState*> &unignoreStates) {
  if (!removedStates.empty()) {
    std::set<ExecutionState *> alt = removedStates;
    for (std::set<ExecutionState*>::const_iterator it = removedStates.begin(),
            ie = removedStates.end(); it != ie; ++it) {
      ExecutionState *es = *it;
      std::set<ExecutionState*>::const_iterator it = statesAtMerge.find(es);
      if (it != statesAtMerge.end()) {
        statesAtMerge.erase(it);
        alt.erase(alt.find(es));
      }
    }
    baseSearcher->update(current, addedStates, alt, ignoreStates, unignoreStates);
  } else {
    baseSearcher->update(current, addedStates, removedStates, ignoreStates, unignoreStates);
  }
}

///

BatchingSearcher::BatchingSearcher(Searcher *_baseSearcher,
        double _timeBudget,
        unsigned _instructionBudget)
: baseSearcher(_baseSearcher),
timeBudget(_timeBudget),
instructionBudget(_instructionBudget),
lastState(0) {

}

BatchingSearcher::~BatchingSearcher() {
  delete baseSearcher;
}

ExecutionState &BatchingSearcher::selectState(bool allowCompact) {
  if (!lastState ||
          (util::estWallTime() - lastStartTime) > timeBudget ||
          (stats::instructions - lastStartInstructions) > instructionBudget) {
    if (lastState) {
      double delta = util::estWallTime() - lastStartTime;
      if (delta > timeBudget * 1.1) {
        std::cerr << "KLEE: increased time budget from " << timeBudget << " to " << delta << "\n";
        timeBudget = delta;
      }
    }
    baseSearcher->update(lastState, addedStates, removedStates, ignoreStates, unignoreStates);
    addedStates.clear();
    removedStates.clear();
    ignoreStates.clear();
    unignoreStates.clear();

    lastState = &baseSearcher->selectState(allowCompact);
    lastStartTime = util::estWallTime();
    lastStartInstructions = stats::instructions;
    return *lastState;
  } else {
    return *lastState;
  }
}

template<typename C1, typename C2>
bool is_disjoint(const C1& a, const C2& b)
{
  typename C1::const_iterator i = a.begin(), ii = a.end();
  typename C2::const_iterator j = b.begin(), jj = b.end();

  while (i != ii && j != jj) {
    if (*i < *j)
      ++i;
    else if (*j < *i)
      ++j;
    else
      return false;
  }

  return true;
}

void BatchingSearcher::update(ExecutionState *current,
        const std::set<ExecutionState*> &addedStates_,
        const std::set<ExecutionState*> &removedStates_,
        const std::set<ExecutionState*> &ignoreStates_,
        const std::set<ExecutionState*> &unignoreStates_) {
  assert(is_disjoint(addedStates_, removedStates_));

  // If there are pending additions before removals, or pending removals before additions,
  // process the pending states first, since those may actually be different states!
  if (!is_disjoint(addedStates, removedStates_) ||
      !is_disjoint(removedStates, addedStates_))
  {
    baseSearcher->update(current, addedStates, removedStates, ignoreStates, unignoreStates);
    addedStates.clear();
    removedStates.clear();
    ignoreStates.clear();
    unignoreStates.clear();
  }

  addedStates.insert(addedStates_.begin(), addedStates_.end());
  removedStates.insert(removedStates_.begin(), removedStates_.end());
  ignoreStates.insert(ignoreStates_.begin(), ignoreStates_.end());
  unignoreStates.insert(unignoreStates_.begin(), unignoreStates_.end());

  if (!lastState || removedStates_.count(lastState) || ignoreStates_.count(lastState)) {
    lastState = 0;
    baseSearcher->update(current, addedStates, removedStates, ignoreStates, unignoreStates);
    addedStates.clear();
    removedStates.clear();
    ignoreStates.clear();
    unignoreStates.clear();
  }
}

/***/

IterativeDeepeningTimeSearcher::IterativeDeepeningTimeSearcher(Searcher *_baseSearcher)
: baseSearcher(_baseSearcher),
time(1.) {
}

IterativeDeepeningTimeSearcher::~IterativeDeepeningTimeSearcher() {
  delete baseSearcher;
}

ExecutionState &IterativeDeepeningTimeSearcher::selectState(bool allowCompact) {
  ExecutionState &res = baseSearcher->selectState(allowCompact);
  startTime = util::estWallTime();
  return res;
}

void IterativeDeepeningTimeSearcher::update(ExecutionState *current,
        const std::set<ExecutionState*> &addedStates,
        const std::set<ExecutionState*> &removedStates,
        const std::set<ExecutionState*> &ignoreStates,
        const std::set<ExecutionState*> &unignoreStates) {
  double elapsed = util::estWallTime() - startTime;

  if (!removedStates.empty()) {
    std::set<ExecutionState *> alt = removedStates;
    for (std::set<ExecutionState*>::const_iterator it = removedStates.begin(),
            ie = removedStates.end(); it != ie; ++it) {
      ExecutionState *es = *it;
      std::set<ExecutionState*>::const_iterator it = pausedStates.find(es);
      if (it != pausedStates.end()) {
        pausedStates.erase(it);
        alt.erase(alt.find(es));
      }
    }
    baseSearcher->update(current, addedStates, alt, ignoreStates, unignoreStates);
  } else {
    baseSearcher->update(current, addedStates, removedStates, ignoreStates, unignoreStates);
  }

  if (current && !removedStates.count(current) && elapsed > time) {
    pausedStates.insert(current);
    baseSearcher->removeState(current);
  }

  if (baseSearcher->empty()) {
    time *= 2;
    std::cerr << "KLEE: increasing time budget to: " << time << "\n";
    baseSearcher->update(0, pausedStates, std::set<ExecutionState*>(), ignoreStates, unignoreStates);
    pausedStates.clear();
  }
}

/***/

InterleavedSearcher::InterleavedSearcher(const std::vector<Searcher*> &_searchers)
: searchers(_searchers),
index(1) {
}

InterleavedSearcher::~InterleavedSearcher() {
  for (std::vector<Searcher*>::const_iterator it = searchers.begin(),
          ie = searchers.end(); it != ie; ++it)
    delete *it;
}

ExecutionState &InterleavedSearcher::selectState(bool allowCompact) {
  Searcher *s = searchers[--index];
  if (index == 0) index = searchers.size();
  ExecutionState* es = &s->selectState(allowCompact);
  return *es;
}

void InterleavedSearcher::update(ExecutionState *current,
        const std::set<ExecutionState*> &addedStates,
        const std::set<ExecutionState*> &removedStates,
        const std::set<ExecutionState*> &ignoreStates,
        const std::set<ExecutionState*> &unignoreStates) {
  for (std::vector<Searcher*>::const_iterator it = searchers.begin(),
          ie = searchers.end(); it != ie; ++it)
    (*it)->update(current, addedStates, removedStates, ignoreStates, unignoreStates);
}

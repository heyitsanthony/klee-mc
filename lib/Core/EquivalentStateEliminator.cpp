#include "ESESupport.h"
#include "StaticRecord.h"
#include "StateRecordManager.h"
#include "llvm/Instruction.h"
#include "klee/Internal/Module/KModule.h"
#include "llvm/Instructions.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KInstIterator.h"
#include "klee/ExecutionState.h"

#include "static/Support.h"
#include "Searcher.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "EquivalentStateEliminator.h"

#include "ControlDependence.h"
#include "StateRecord.h"
#include "Sugar.h"
#include "llvm/Module.h"
#include "llvm/Support/CommandLine.h"

#include "Executor.h"
#include "ESEStats.h"

#include <fstream>
#include <iostream>
#include <algorithm>
#include <list>
#include <typeinfo>

using namespace klee;

cl::opt<bool>
ESEPrintCoverStats("ese-cover-stats",
        cl::init(false));

cl::opt<bool>
ESEPrintStats("ese-stats",
        cl::init(false));

cl::opt<bool>
ESEDebug("ese-debug",
        cl::init(false));

cl::opt<bool>
ESEDebugVerbose("ese-debug-verbose",
        cl::init(false));

void EquivalentStateEliminator::setup(ExecutionState* state, std::set<ExecutionState*>& holds) {
  ESEStats::copyTimer.start();

  StateRecord* prevrec = state->rec;
  StateRecord* newrec = stateRecordManager->newStateRecord(state, holds);

  if (ESESupport::isRecStart(state)) {

    if (prevrec && prevrec->staticRecord->isPredicate()) {
      if (prevrec->staticRecord->iPostDomIsSuperExit) {
        state->controlDependenceStack.clear();
        prevrec->isExitControl = true;
      } else {
        if (!state->controlDependenceStack.empty()) {
          StateRecord* cdstop = state->controlDependenceStack.back();

          if (((prevrec->staticRecord->ipostdom == cdstop->staticRecord->ipostdom) && (prevrec->callers == cdstop->callers)) ||
                  ((prevrec->staticRecord->iPostDomIsExit && cdstop->staticRecord->iPostDomIsExit) && (prevrec->callers == cdstop->callers))) {
            state->controlDependenceStack.pop_back();
          }
        }

        state->controlDependenceStack.push_back(prevrec);
      }
    }

    if (!state->controlDependenceStack.empty()) {
      StateRecord* cdstop = state->controlDependenceStack.back();

      if (((newrec->staticRecord == cdstop->staticRecord->ipostdom) && (newrec->callers == cdstop->callers)) ||
              ((prevrec->staticRecord->isReturn() && cdstop->staticRecord->iPostDomIsExit) && (prevrec->callers == cdstop->callers))) {
        state->controlDependenceStack.pop_back();

      }
    }

    if (!state->controlDependenceStack.empty()) {
      StateRecord* cdstop = state->controlDependenceStack.back();
      newrec->regularControl = cdstop->branchRead;
    }
  }

  ESESupport::checkControlDependenceStack(state);
  ESEStats::copyTimer.stop();
}

void EquivalentStateEliminator::complete() {
  if (ESEPrintStats) stats();
  if (ESEPrintCoverStats) coverStats();
}

void EquivalentStateEliminator::update(ExecutionState *current,
        std::set<ExecutionState*> &addedStates,
        std::set<ExecutionState*> &removedStates,
        std::set<ExecutionState*> &ignoreStates,
        std::set<ExecutionState*> &unignoreStates) {
  if ((ESEStats::updateCount++ % 20000) == 0) {
    if (ESEPrintStats) stats();
    if (ESEPrintCoverStats) coverStats();
  }

  ESEStats::reviseTimer.start();

  std::set<ExecutionState*> updatedStates;

  foreach(it, addedStates.begin(), addedStates.end()) {
    assert(!allholds.count(*it));
  }

  if (current) {
    if (removedStates.find(current) == removedStates.end()) {
      assert(!allholds.count(current));
    }
  }

  std::set<ExecutionState*> holds;

  foreach(it, addedStates.begin(), addedStates.end()) {
    ExecutionState* es = *it;
    es->rec->curreads.clear();
    stateRecordManager->execute(es->rec, holds);

    if (ESESupport::isRecStart(es) || es->rec->isShared) {
      assert(!updatedStates.count(es));
      updatedStates.insert(es);
    }
  }

  if (current != NULL) {
    if (removedStates.find(current) == removedStates.end()) {
      current->rec->curreads.clear();
      stateRecordManager->execute(current->rec, holds);

      if (ESESupport::isRecStart(current) || current->rec->isShared) {
        updatedStates.insert(current);
      }
    }
  }

  ESEStats::setupTimer.start();

  foreach(it, updatedStates.begin(), updatedStates.end()) {
    ExecutionState* updatedState = *it;
    setup(updatedState, holds);
  }
  ESEStats::setupTimer.stop();

  ESEStats::handleTimer.start();
  std::set<ExecutionState*> prunes;
  std::set<ExecutionState*> releases;
  std::list<StateRecord*> terminated;

  foreach(it, updatedStates.begin(), updatedStates.end()) {
    ExecutionState* updatedState = *it;
    if (prunes.count(updatedState)) continue;

    std::set<StateRecord*> toTerminate;
    stateRecordManager->check(updatedState, prunes, releases, toTerminate);
    stateRecordManager->terminate(toTerminate, terminated, releases);
  }

  foreach(it, removedStates.begin(), removedStates.end()) {
    ExecutionState* removedState = *it;
    stateRecordManager->terminate(removedState->rec, terminated, releases);
  }

  while (!terminated.empty()) {
    StateRecord* rec = terminated.front();
    terminated.pop_front();

    std::set<StateRecord*> toTerminate;
    stateRecordManager->check(rec, prunes, releases, toTerminate);
    stateRecordManager->terminate(toTerminate, terminated, releases);
  }

  ESEStats::handleTimer.stop();

  foreach(it, prunes.begin(), prunes.end()) {
    ExecutionState* state = *it;
    removedStates.insert(state);
  }

  foreach(it, holds.begin(), holds.end()) {
    ExecutionState* state = *it;
    if (!releases.count(state)) {
      bool b = allholds.insert(state).second;
      assert(b);
      ignoreStates.insert(state);
    }
  }

  foreach(it, releases.begin(), releases.end()) {
    ExecutionState* state = *it;
    if (!holds.count(state)) {
      bool b = allholds.erase(state);
      assert(b);
      unignoreStates.insert(state);
    }
  }

  assert(Support::isDisjoint(ignoreStates, unignoreStates));
  assert(Support::isDisjoint(ignoreStates, removedStates));
  assert(Support::isDisjoint(addedStates, unignoreStates));


  ESEStats::reviseTimer.stop();
}

/*
void EquivalentStateEliminator::update(ExecutionState *current,
        std::set<ExecutionState*> &addedStates,
        std::set<ExecutionState*> &removedStates,
        std::set<ExecutionState*> &ignoreStates,
        std::set<ExecutionState*> &unignoreStates) {
  if ((ESEStats::updateCount++ % 20000) == 0) {
    if (ESEPrintStats) stats();
    if (ESEPrintCoverStats) coverStats();
  }

  ESEStats::reviseTimer.start();

  std::set<ExecutionState*> updatedStates;
  

  foreach(it, addedStates.begin(), addedStates.end()) {
    assert(!allholds.count(*it));
  }

  if (current) {
    if (removedStates.find(current) == removedStates.end()) {
      assert(!allholds.count(current));
    }
  }

  std::set<ExecutionState*> holds;
  foreach(it, addedStates.begin(), addedStates.end()) {
    ExecutionState* es = *it;    
    es->rec->curreads.clear();
    stateRecordManager->execute(es->rec, holds);

    if (ESESupport::isRecStart(es) || es->rec->isShared) {
      assert(!updatedStates.count(es));
      updatedStates.insert(es);
    }
  }

  if (current != NULL) {
    if (removedStates.find(current) == removedStates.end()) {
      current->rec->curreads.clear();
      stateRecordManager->execute(current->rec, holds);

      if (ESESupport::isRecStart(current) || current->rec->isShared) {
        updatedStates.insert(current);
      }
    }
  }

  ESEStats::setupTimer.start();
  foreach(it, updatedStates.begin(), updatedStates.end()) {
    ExecutionState* updatedState = *it;
    setup(updatedState, holds);
  }
  ESEStats::setupTimer.stop();

  ESEStats::handleTimer.start();
  std::set<ExecutionState*> prunes;
  std::set<ExecutionState*> releases;

  foreach(it, removedStates.begin(), removedStates.end()) {
    ExecutionState* removedState = *it;
    stateRecordManager->terminate(removedState->rec, prunes, releases);
  }

  assert(Support::isDisjoint(updatedStates, releases));

  foreach(it, updatedStates.begin(), updatedStates.end()) {
    ExecutionState* updatedState = *it;
    if (prunes.count(updatedState)) continue;

    stateRecordManager->check(updatedState, prunes, releases);
  }
  ESEStats::handleTimer.stop();

  foreach(it, prunes.begin(), prunes.end()) {
    ExecutionState* state = *it;
    removedStates.insert(state);
  }

  foreach(it, holds.begin(), holds.end()) {
    ExecutionState* state = *it;
    if (!releases.count(state)) {
      ignoreStates.insert(state);
      allholds.insert(state);
    }
  }

  foreach(it, releases.begin(), releases.end()) {
    ExecutionState* state = *it;
    allholds.erase(state);
    unignoreStates.insert(state);
  }

  assert(Support::isDisjoint(ignoreStates, unignoreStates));
  assert(Support::isDisjoint(ignoreStates, removedStates));
  assert(Support::isDisjoint(addedStates, unignoreStates));

  ESEStats::reviseTimer.stop();
}
 */
EquivalentStateEliminator::EquivalentStateEliminator(Executor* _executor, KModule* _kmodule, const std::set<ExecutionState*>& _states)
: executor(_executor),
staticRecordManager(new StaticRecordManager(_kmodule)),
stateRecordManager(new StateRecordManager(this)),
controlDependence(new ControlDependence(_kmodule->module, staticRecordManager)),
kmodule(_kmodule), initialStateRecord(0), states(_states) {

  ESEStats::debug = ESEDebug;
  ESEStats::debugVerbose = ESEDebugVerbose;
  ESEStats::printStats = ESEPrintStats;
  ESEStats::printCoverStats = ESEPrintCoverStats;
}

#include "CallStringHasher.h"
#include "ESEStats.h"
#include "StaticRecord.h"
#include "klee/ExecutionState.h"
#include "StateRecordComparer.h"
#include "static/Support.h"
#include "StateRecord.h"
#include "SegmentGraph.h"
#include "StateRecordManager.h"

using namespace klee;

StateRecordManager::StateRecordManager(EquivalentStateEliminator* _elim) :
segmentHoldGraph(new SegmentGraph()), elim(_elim) {

}

void StateRecordManager::unhold(StateRecord* unxrec, StateRecord* rec) {
  assert(segmentHoldGraph->hasEdge(unxrec->currentSegment, rec->currentSegment));
  segmentHoldGraph->removeEdge(unxrec->currentSegment, rec->currentSegment);
  unxrec->holder = NULL;
}

bool StateRecordManager::attemptHold(StateRecord* unxrec, StateRecord* xrec) {
  assert(unxrec);
  assert(xrec);
  assert(!unxrec->holder);

  if (!segmentHoldGraph->checkCycleIfAdd(unxrec->currentSegment, xrec->currentSegment)) {
    segmentHoldGraph->addEdge(unxrec->currentSegment, xrec->currentSegment);
    assert(segmentHoldGraph->hasEdge(unxrec->currentSegment, xrec->currentSegment));

    unxrec->holder = xrec;
    return true;
  } else {
    /*if (unxrec->currentSegment != xrec->currentSegment) {
      std::cout << "nodes=" << segmentHoldGraph->nodeCount() << std::endl;
      std::cout << unxrec->currentSegment << " " << xrec->currentSegment << std::endl;
      segmentHoldGraph->writeDOTGraph();
      exit(1);
    }*/
  }

  return false;
}

void StateRecordManager::execute(StateRecord* rec,
        std::set<ExecutionState*>& holds) {

  if (!rec->isExecuted()) {
    rec->getComparer()->notifyExecuted(rec, holds);
  }
}

void StateRecordManager::check(ExecutionState* state,
        std::set<ExecutionState*>& prunes,
        std::set<ExecutionState*>& releases,
        std::set<StateRecord*>& toTerminate) {

  getComparer(state)->check(state, prunes, releases, toTerminate);

}

void StateRecordManager::check(StateRecord* trec,
        std::set<ExecutionState*>& prunes,
        std::set<ExecutionState*>& releases,
        std::set<StateRecord*>& toTerminate) {

  trec->getComparer()->check(trec, prunes, releases, toTerminate);

}

void StateRecordManager::terminate(const std::set<StateRecord*>& toTerminate,
        std::list<StateRecord*>& terminated,
        std::set<ExecutionState*>& releases) {

  foreach(it, toTerminate.begin(), toTerminate.end()) {
    StateRecord* rec = *it;
    if (rec->isTerminated()) continue;
    terminate(rec, terminated, releases);
  }
}

void StateRecordManager::terminate(StateRecord* toTerminate,
        std::list<StateRecord*>& terminated,
        std::set<ExecutionState*>& releases) {

  assert(!toTerminate->isTerminated());

  if (ESEStats::debug) {
    std::cout << "TERMINATE: " << toTerminate << " ";
    std::cout << toTerminate->staticRecord->function->getNameStr();
    std::cout << " " << toTerminate->staticRecord->name() << std::endl;
  }

  ESEStats::terminateTimer.start();

  StateRecord* rec = toTerminate;
  while (rec && rec->haveAllChildrenTerminatedOrPruned()) {
    assert(!rec->isTerminated());

    rec->terminate();
    rec->getComparer()->notifyTerminated(rec, releases);
    terminated.push_back(rec);

    rec = rec->parent;
  }

  ESEStats::terminateTimer.stop();
}

StateRecord* StateRecordManager::newStateRecord(ExecutionState* state,
        std::set<ExecutionState*>& holds) {

  StateRecord* prevrec = state->rec;
  StateRecord* newrec = new StateRecord(elim, state, getComparer(state));

  if (prevrec) {
    newrec->segments.insert(prevrec->segments.begin(), prevrec->segments.end());

    if (prevrec->isShared) {
      newrec->currentSegment = ++StateRecord::segmentCount;
      newrec->segments.insert(newrec->currentSegment);
      segmentHoldGraph->addEdge(prevrec->currentSegment, newrec->currentSegment);
    } else {
      newrec->currentSegment = prevrec->currentSegment;
    }

    prevrec->staticRecord->cover(false);
    prevrec->addChild(newrec);
  }

  newrec->parent = state->rec;
  state->rec = newrec;

  newrec->getComparer()->notifyNew(newrec, holds);

  return newrec;
}

StateRecordComparer* StateRecordManager::getComparer(ExecutionState* state) {

  unsigned hash = CallStringHasher::hash(state);

  StateRecordComparer * comparer = recCache[hash];
  if (comparer == NULL) {
    std::vector<Instruction*> callstring;
    Instruction* inst = state->pc->inst;

    foreach(it, state->stack.begin(), state->stack.end()) {
      const StackFrame& sf = *it;
      if (sf.caller) {
        callstring.push_back(sf.caller->inst);
      }
    }

    comparer = new StateRecordComparer(this, callstring, inst);
    recCache[hash] = comparer;
  }

  return comparer;
}
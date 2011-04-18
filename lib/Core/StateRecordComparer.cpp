#include "ESEStats.h"
#include "ESESupport.h"
#include "SegmentGraph.h"
#include "StaticRecord.h"
#include "StateRecordComparer.h"
#include "klee/ExecutionState.h"
#include "StateRecordManager.h"
#include "StateRecord.h"
#include "Memory.h"
#include "static/Support.h"
#include "LiveSetCache.h"

#include <set>
#include <vector>
#include <list>

using namespace klee;

StateRecordComparer::StateRecordComparer(
        StateRecordManager* _stateRecordManager,
        const std::vector<Instruction*>& _callstring,
        Instruction* _inst)

: liveSetCache(new LiveSetCache()),
callstring(_callstring),
inst(_inst),
stateRecordManager(_stateRecordManager),
prevCheckRepRecCount(0) {

  checkRep();
}

StateRecordComparer::StateRecordComparer(
        StateRecordManager* _stateRecordManager)

: liveSetCache(new LiveSetCache()),
inst(NULL),
stateRecordManager(_stateRecordManager),
prevCheckRepRecCount(0) {

  checkRep();
}

void StateRecordComparer::check(StateRecord* trec,
        std::set<ExecutionState*>& prunes,
        std::set<ExecutionState*>& releases,
        std::set<StateRecord*>& toTerminate) {

  checkRep();

  std::vector<StateRecord*> recs;

  foreach(it, holdSet.begin(), holdSet.end()) {
    StateRecord* rec = *it;
    recs.push_back(rec);
  }

  foreach(it, releaseSet.begin(), releaseSet.end()) {
    StateRecord* rec = *it;
    recs.push_back(rec);
  }

  foreach(it, recs.begin(), recs.end()) {
    StateRecord* rec = *it;
    ExecutionState* state = rec->getState();
    assert(state);

    if (trec->isEquiv(state)) {
      prune(trec, state->rec, prunes, releases, toTerminate);
    }
  }

  checkRep();
}

void StateRecordComparer::check(ExecutionState* state,
        std::set<ExecutionState*>& prunes,
        std::set<ExecutionState*>& releases,
        std::set<StateRecord*>& toTerminate) {

  checkRep();

  assert(holdSet.count(state->rec) || releaseSet.count(state->rec));
  assert(!pendingSet.count(state->rec) && !terminatedSet.count(state->rec));
  assert(!prunedSet.count(state->rec));

  if (StateRecord * trec = liveSetCache->check(state)) {
    prune(trec, state->rec, prunes, releases, toTerminate);
  }

  checkRep();
}

void StateRecordComparer::prune(StateRecord* trec, StateRecord* toPrune,
        std::set<ExecutionState*>& prunes,
        std::set<ExecutionState*>& releases,
        std::set<StateRecord*>& toTerminate) {
  
  if (ESEStats::debug) {
    std::cout << "PRUNING REC: sts=" << trec << " ";
    std::cout << trec->inst->getParent()->getParent()->getNameStr() << " ";
    std::cout << trec->inst->getParent()->getNameStr() << " ";
    std::cout << *(trec->inst) << std::endl;
  }

  if (ESEStats::debugVerbose) trec->printCallString();

  ExecutionState* state = toPrune->getState();
  assert(state);
  assert(terminatedSet.count(trec));
  assert(!toPrune->isPruned());
  assert(!toPrune->isTerminated());

  trec->addToPrunedSet(toPrune);
  toPrune->setPruned();
  trec->copyLiveReadsInto(state);

  if (holdSet.count(toPrune)) {
    assert(toPrune->holder);
    assert(holdSet.count(toPrune));
    assert(toPrune->getState());

    stateRecordManager->unhold(toPrune, toPrune->holder);
    holdSet.erase(toPrune);
    releases.insert(state);
  } else if (releaseSet.count(toPrune)) {
    releaseSet.erase(toPrune);
  } else {
    assert(false);
  }

  prunes.insert(state);
  assert(!toPrune->parent->isTerminated());
  toTerminate.insert(toPrune->parent);
  toPrune->clearState();
  prunedSet.insert(toPrune);
}

void StateRecordComparer::notifyNew(StateRecord* unxrec,
        std::set<ExecutionState*>& holds) {

  checkRep();

  assert(unxrec);
  assert(!pendingSet.count(unxrec) && !holdSet.count(unxrec) &&
          !releaseSet.count(unxrec));
  assert(!terminatedSet.count(unxrec) && !prunedSet.count(unxrec));
  assert(!unxrec->holder);
  assert(unxrec->getState());
  assert(this == unxrec->getComparer());

  foreach(it, pendingSet.begin(), pendingSet.end()) {
    StateRecord* xrec = *it;
    assert(xrec);

    if (stateRecordManager->attemptHold(unxrec, xrec)) {
      holds.insert(unxrec->getState());
      holdSet.insert(unxrec);
      checkRep();
      return;
    }
  }

  assert(unxrec->getState());
  releaseSet.insert(unxrec);

  checkRep();
}

void StateRecordComparer::notifyExecuted(StateRecord* rec,
        std::set<ExecutionState*>& holds) {

  checkRep();

  assert(rec);
  assert(releaseSet.count(rec));
  assert(!pendingSet.count(rec) && !holdSet.count(rec));
  assert(!terminatedSet.count(rec) && !prunedSet.count(rec));
  assert(rec->getState());
  assert(!rec->holder);
  assert(!rec->terminated);
  assert(this == rec->getComparer());

  rec->execute();
  releaseSet.erase(rec);
  pendingSet.insert(rec);

  std::vector<StateRecord*> rm;

  foreach(it, releaseSet.begin(), releaseSet.end()) {
    StateRecord* unxrec = *it;
    assert(unxrec);
    assert(unxrec->getState());

    if (stateRecordManager->attemptHold(unxrec, rec)) {
      if (ESEStats::debug) {
        std::cout << "HOLD: state=" << unxrec->getState() << " rec=" << unxrec << " ";
        std::cout << unxrec->staticRecord->function->getNameStr() << " ";
        std::cout << unxrec->staticRecord->name();
        std::cout << " holds=" << holds.size();
        std::cout << " segments=";
        Support::print(unxrec->segments);
        std::cout << " curseg=" << unxrec->currentSegment;
        std::cout << std::endl;
      }

      rm.push_back(unxrec);
      holdSet.insert(unxrec);
      holds.insert(unxrec->getState());
    }
  }

  Support::eraseAll(releaseSet, rm);

  assert(!rec->getState());
  checkRep();
}

void StateRecordComparer::notifyTerminated(StateRecord* rec,
        std::set<ExecutionState*>& releases) {

  checkRep();
  assert(rec);

  assert(releaseSet.count(rec) || pendingSet.count(rec) || holdSet.count(rec));
  assert(!terminatedSet.count(rec));
  assert(!prunedSet.count(rec));
  assert(this == rec->getComparer());

  if (pendingSet.count(rec)) {
    pendingSet.erase(rec);

    std::vector<StateRecord*> rm;

    foreach(it, holdSet.begin(), holdSet.end()) {
      StateRecord* unxrec = *it;
      assert(unxrec);

      if (unxrec->holder == rec) {
        assert(unxrec->getState());
        stateRecordManager->unhold(unxrec, rec);

        bool foundHolder = false;

        foreach(it, pendingSet.begin(), pendingSet.end()) {
          StateRecord* xrec = *it;
          assert(xrec);

          if (stateRecordManager->attemptHold(unxrec, xrec)) {
            foundHolder = true;
            break;
          }
        }

        if (!foundHolder) {
          if (ESEStats::debug) {
            std::cout << "RELEASE: " << unxrec << " ";
            std::cout << unxrec->staticRecord->function->getNameStr() << " ";
            std::cout << unxrec->staticRecord->name() << std::endl;
          }

          rm.push_back(unxrec);
          releases.insert(unxrec->getState());
          assert(unxrec->getState());
          releaseSet.insert(unxrec);
        }
      }
    }

    Support::eraseAll(holdSet, rm);
  } else if (releaseSet.count(rec)) {
    releaseSet.erase(rec);
    assert(rec->getState());
    rec->clearState();
  } else if (holdSet.count(rec)) {
    assert(rec->holder);
    assert(holdSet.count(rec));
    assert(rec->getState());

    releases.insert(rec->getState());
    stateRecordManager->unhold(rec, rec->holder);
    holdSet.erase(rec);
    rec->clearState();
  }

  liveSetCache->add(rec);
  terminatedSet.insert(rec);

  checkRep();
}

void StateRecordComparer::notifyReterminated(StateRecord * rec) {
  assert(terminatedSet.count(rec));
  liveSetCache->readd(rec);
}

void StateRecordComparer::checkRep() {
  return;
  std::vector<std::set<StateRecord*>*> sets;
  sets.push_back(&pendingSet);
  sets.push_back(&holdSet);
  sets.push_back(&releaseSet);
  sets.push_back(&terminatedSet);
  sets.push_back(&prunedSet);

  for (unsigned i = 0; i < sets.size(); i++) {
    std::set<StateRecord*>* s1 = sets[i];

    for (unsigned j = i + 1; j < sets.size(); j++) {
      std::set<StateRecord*>* s2 = sets[j];

      assert(Support::isDisjoint(*s1, *s2));
    }
  }

  foreach(it, holdSet.begin(), holdSet.end()) {
    StateRecord* rec = *it;
    assert(rec->holder);
    assert(rec->getState());
    assert(pendingSet.count(rec->holder));
    assert(stateRecordManager->segmentHoldGraph->
            hasEdge(rec->currentSegment, rec->holder->currentSegment));
  }

  foreach(it, pendingSet.begin(), pendingSet.end()) {
    StateRecord* rec = *it;
    assert(!rec->holder);
    assert(!rec->getState());
  }

  foreach(it, terminatedSet.begin(), terminatedSet.end()) {
    StateRecord* rec = *it;
    assert(!rec->holder);
    assert(!rec->getState());
  }

  foreach(it, prunedSet.begin(), prunedSet.end()) {
    StateRecord* rec = *it;
    assert(!rec->holder);
    assert(!rec->getState());
  }

  foreach(it, releaseSet.begin(), releaseSet.end()) {
    StateRecord* rec = *it;
    assert(!rec->holder);
    assert(rec->getState());
  }

  unsigned recCount = holdSet.size() + pendingSet.size() +
          terminatedSet.size() + prunedSet.size() + releaseSet.size();
  assert(recCount >= prevCheckRepRecCount);
  prevCheckRepRecCount = recCount;
}
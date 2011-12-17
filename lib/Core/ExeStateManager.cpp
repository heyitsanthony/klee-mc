#include "Executor.h"
#include "ExeStateManager.h"
#include "Searcher.h"
#include "UserSearcher.h"
#include "MemUsage.h"
#include "klee/Common.h"
#include "static/Sugar.h"

#include <algorithm>
#include <iostream>

using namespace llvm;
using namespace klee;

ExeStateManager::ExeStateManager()
: nonCompactStateCount(0), searcher(0)
{}

ExeStateManager::~ExeStateManager()
{
  if (searcher) delete searcher;
}

ExecutionState* ExeStateManager::selectState(bool allowCompact)
{
  ExecutionState* ret;

  assert (!empty());
  ret = &searcher->selectState(allowCompact);
  assert((allowCompact || !ret->isCompactForm) && "compact state chosen");

  return ret;
}

void ExeStateManager::setupSearcher(Executor* exe)
{
	assert (!searcher && "Searcher already inited");
	searcher = UserSearcher::constructUserSearcher(*exe);
	searcher->update(
		NULL,
		Searcher::States(
			states,
			Searcher::States::emptySet,
			Searcher::States::emptySet,
			Searcher::States::emptySet));
}

void ExeStateManager::teardownUserSearcher(void)
{
  assert (searcher);
  delete searcher;
  searcher = 0;
}

void ExeStateManager::setInitialState(
  Executor* exe,
  ExecutionState* initialState, bool replay)
{
	assert (empty());

	if (replay) {
		// remove initial state from ptree
		states.insert(initialState);
		removedStates.insert(initialState);
		notifyCurrent(exe, NULL); /* XXX ??? */
	} else {
		states.insert(initialState);
		++nonCompactStateCount;
	}
}

void ExeStateManager::setWeights(double weight)
{
	foreach (it, states.begin(), states.end())
		(*it)->weight = weight;
}

void ExeStateManager::add(ExecutionState* es)
{
  if (!es->isCompactForm) ++nonCompactStateCount;
  addedStates.insert(es);
}

void ExeStateManager::remove(ExecutionState* s)
{
    removedStates.insert(s);
}

void ExeStateManager::dropAdded(ExecutionState* es)
{
  ExeStateSet::iterator it = addedStates.find(es);
  assert (it != addedStates.end());
  addedStates.erase(it);
}

void ExeStateManager::notifyCurrent(Executor* exe, ExecutionState *current)
{
  if (searcher) {
    searcher->update(current, getStates());
    ignoreStates.clear();
    unignoreStates.clear();
  }

  states.insert(addedStates.begin(), addedStates.end());
  nonCompactStateCount += std::count_if(
    addedStates.begin(),
    addedStates.end(),
    std::mem_fun(&ExecutionState::isNonCompactForm_f));
  addedStates.clear();

  ExecutionState* root_to_be_removed = 0;
  foreach (it, removedStates.begin(), removedStates.end()) {
    ExecutionState *es = *it;

    ExeStateSet::iterator it2 = states.find(es);
    assert(it2!=states.end());
    states.erase(it2);

    // this deref must happen before delete
    if (!es->isCompactForm) --nonCompactStateCount;
    exe->removePTreeState(es, &root_to_be_removed);
  }

  ExecutionState* es = root_to_be_removed;
  if (es) exe->removeRoot(es);
  removedStates.clear();
  replacedStates.clear();

//  klee_message("Updated to: %s\n", states2str(states).c_str());
}

void ExeStateManager::replaceState(ExecutionState* old_s, ExecutionState* new_s)
{
  addedStates.insert(new_s);
  removedStates.insert(old_s);
  replacedStates[old_s] = new_s;
}

/* don't bother queueing up state for deletion, get rid of it immediately */
void ExeStateManager::replaceStateImmediate(
  ExecutionState* old_s, ExecutionState* new_s)
{
  assert (!isRemovedState(new_s));
  addedStates.insert(new_s);
  assert (isAddedState(old_s));
  addedStates.erase(old_s);
  replacedStates[old_s] = new_s;
}

bool ExeStateManager::isAddedState(ExecutionState* s) const
{
  return (addedStates.count(s) > 0);
}

bool ExeStateManager::isRemovedState(ExecutionState* s) const
{
  return (removedStates.count(s) > 0);
}

ExecutionState* ExeStateManager::getReplacedState(ExecutionState* s) const
{
  ExeStateReplaceMap::const_iterator it;
  it = replacedStates.find(s);
  if (it != replacedStates.end()) return it->second;
  return NULL;
}

void ExeStateManager::compactStates(ExecutionState* &state, uint64_t maxMem)
{
  // compact instead of killing
  std::vector<ExecutionState*> arr(nonCompactStateCount);
  unsigned i = 0;
  foreach (si, states.begin(), states.end()) {
    if((*si)->isCompactForm) continue;
    arr[i++] = *si;
  }

   // a rough measure
  unsigned s = nonCompactStateCount + ((size()-nonCompactStateCount)/16);
  uint64_t mbs = getMemUsageMB();
  unsigned toCompact = std::max(
    (uint64_t)1, (uint64_t)s - s * maxMem / mbs);
  toCompact = std::min(toCompact, (unsigned) nonCompactStateCount);
  klee_warning("compacting %u states (over memory cap)", toCompact);

  std::partial_sort(
    arr.begin(), arr.begin() + toCompact, arr.end(), KillOrCompactOrdering());

  for (i = 0; i < toCompact; ++i) {
    ExecutionState* original = arr[i];
    ExecutionState* compacted = original->compact();
    compacted->coveredNew = false;
    compacted->coveredLines.clear();
    compacted->ptreeNode = original->ptreeNode;
    replaceState(original, compacted);
    if (state == original) state = compacted;
  }
}

Searcher::States ExeStateManager::getStates(void) const
{
	return Searcher::States(
		addedStates,
		removedStates,
		ignoreStates,
		unignoreStates);
}

#include "Executor.h"
#include "ExeStateManager.h"
#include "Searcher.h"
#include "MemUsage.h"
#include "klee/Common.h"
#include "static/Sugar.h"
#include "PTree.h"

#include <llvm/Support/CommandLine.h>

#include <algorithm>
#include <iostream>

using namespace llvm;
using namespace klee;

namespace { cl::opt<bool> UseYield("use-yield", cl::init(true)); }

ExeStateManager::ExeStateManager()
: nonCompactStateCount(0)
, onlyNonCompact(false)
{}

ExeStateManager::~ExeStateManager()
{
	if (!searcher && !pathTree) return;

	/* flush all pending states */
	commitQueue(NULL);

	/* wake up all yielding */
	while (!yieldedStates.empty()) {
		popYieldedState();
		commitQueue(NULL);
	}

	/* queue erase all states */
	for (auto &es : states)  queueRemove(es);

	/* flush all pending states (erase all) */
	commitQueue(NULL);
}

ExecutionState* ExeStateManager::popYieldedState(void)
{
	ExecutionState	*es;

	assert (addedStates.empty());
	assert (!yieldedStates.empty());

	es = *yieldedStates.begin();

	yieldedStates.erase(es);
	queueAdd(es);
	searcher->update(NULL, getStates());
	states.insert(es);
	addedStates.clear();

	return es;
}

ExecutionState* ExeStateManager::selectState(void)
{
	ExecutionState* ret;
	bool		allowCompact = !onlyNonCompact;

	assert (!empty());

	/* only yielded states left? well.. pop one */
	if (states.empty()) popYieldedState();

	ret = searcher->selectState(allowCompact);
	assert (ret && "should choose *something*");

	if (ret->checkCanary() == false) {
		std::cerr << "BAD CANARY ON ST=" << (void*)ret << '\n';
		assert (ret->checkCanary());
	}
	assert((allowCompact || !ret->isCompact()) && "compact state chosen");

	return ret;
}

void ExeStateManager::setupSearcher(std::unique_ptr<Searcher> s)
{
	assert (!searcher && "Searcher already inited");
	searcher = std::move(s);
	searcher->update(NULL, Searcher::States(states));
}

void ExeStateManager::teardownUserSearcher(void)
{
	assert (searcher);
	searcher = nullptr;
}

void ExeStateManager::setInitialState(ExecutionState* initialState)
{
	assert (empty());

	pathTree = std::make_unique<PTree>(initialState);

	initialState->ptreeNode = pathTree->root;

	states.insert(initialState);
	nonCompactStateCount++;
}

void ExeStateManager::setWeights(double weight)
{
	for (auto &es : states) es->weight = weight;
}

void ExeStateManager::queueAdd(ExecutionState* es)
{
	assert (es->checkCanary());
	addedStates.insert(es);
}

void ExeStateManager::queueSplitAdd(
	shared_ptnode	&ptn,
	ExecutionState	*initialState,
	ExecutionState	*newState)
{
	pathTree->splitStates(ptn, initialState, newState);
	queueAdd(newState);
}


void ExeStateManager::queueRemove(ExecutionState* s)
{
	if (isAddedState(s)) {
		dropAdded(s);
		return;
	}
	removedStates.insert(s);
}

/* note: only a state that has already been added can call yield--
 * this means 's' is gauranteed to be the states list and not in addedStates */
void ExeStateManager::yield(ExecutionState* s)
{
	/* do not yield if we're low on states */
	if (!UseYield || states.size() == 1)
		return;

	std::cerr << "[ExeMan] Yielding st=" << (void*)s << '\n';
	assert (!s->isCompact() && "yielding compact state? HOW?");

	compactState(s);

	/* queue yielded state for removing from sched stack */
	yieldStates.insert(s);

	/* NOTE: states and yieldedStates are disjoint */
}

void ExeStateManager::forceYield(ExecutionState* s)
{
	yieldStates.insert(s);

	ExeStateSet		dummy;
	Searcher::States	ss(dummy, yieldStates);

	searcher->update(NULL, ss);
	states.erase(s);
}

void ExeStateManager::dropAdded(ExecutionState* es)
{
	dropAddedDirect(es);
	pathTree->remove(es->ptreeNode);
	delete es;
}

void ExeStateManager::dropAddedDirect(ExecutionState* es)
{
	ExeStateSet::iterator it = addedStates.find(es);
	assert (it != addedStates.end());
	addedStates.erase(it);
}


void ExeStateManager::commitQueue(ExecutionState *current)
{
	ExecutionState	*root_to_be_removed;

	root_to_be_removed = NULL;

	if (searcher != NULL)
		searcher->update(current, getStates());

	for (auto es : removedStates) {
		auto it = states.find(es);
		assert (it != states.end());
		states.erase(it);

		if (!es->isCompact())
			--nonCompactStateCount;

		// delete es
		removePTreeState(es, &root_to_be_removed);
	}

	if (root_to_be_removed) removeRoot(root_to_be_removed);

	if (!addedStates.empty()) {
		states.insert(addedStates.begin(), addedStates.end());
		nonCompactStateCount += std::count_if(
			addedStates.begin(),
			addedStates.end(),
			std::mem_fun(&ExecutionState::isNonCompact));
		addedStates.clear();
	}

	yieldedStates.insert(yieldStates.begin(), yieldStates.end());
	yieldStates.clear();

	removedStates.clear();
	replacedStates.clear();

	if (getNonCompactStateCount() == 0 && !empty())
		onlyNonCompact = false;
}

void ExeStateManager::replaceState(ExecutionState* old_s, ExecutionState* new_s)
{
	addedStates.insert(new_s);
	removedStates.insert(old_s);
	replacedStates[old_s] = new_s;
}

/* don't bother queueing up state for deletion, get rid of it immediately */
void ExeStateManager::replaceStateImmediate(
	ExecutionState* old_s, ExecutionState* new_s,
	ExecutionState** root_to_be_removed)
{
	assert (!isRemovedState(new_s));
	addedStates.insert(new_s);
	assert (isAddedState(old_s));
	addedStates.erase(old_s);
	replacedStates[old_s] = new_s;

	removePTreeState(old_s, root_to_be_removed);
}


void ExeStateManager::removeRoot(ExecutionState* es)
{
	ExecutionState* ns;

	ns = getReplacedState(es);
	if (ns == NULL) {
		pathTree->removeRoot(es);
		return;
	}

	pathTree->updateReplacement(ns, es);
	delete es;
}

void ExeStateManager::removePTreeState(
	ExecutionState* es,
	ExecutionState** root_to_be_removed)
{
	ExecutionState	*ns;

	if (pathTree->isRoot(es)) {
		*root_to_be_removed = es;
		return;
	}

	assert(
		es->ptreeNode->getData() == es ||
		es->ptreeNode->getData() == NULL);

	ns = getReplacedState(es);

	if (ns == NULL) {
		pathTree->remove(es->ptreeNode);
	} else {
		pathTree->updateReplacement(ns, es);
	}

	delete es;
}

bool ExeStateManager::isAddedState(ExecutionState* s) const
{ return (addedStates.count(s) > 0); }

bool ExeStateManager::isRemovedState(ExecutionState* s) const
{ return (removedStates.count(s) > 0); }

ExecutionState* ExeStateManager::getReplacedState(ExecutionState* s) const
{
	ExeStateReplaceMap::const_iterator it;
	it = replacedStates.find(s);
	if (it != replacedStates.end()) return it->second;
	return NULL;
}

void ExeStateManager::compactStates(unsigned toCompact)
{
	std::vector<ExecutionState*> arr(nonCompactStateCount);
	unsigned i = 0;

	for (auto es : states) {
		if (es->isCompact())
			continue;
		arr[i++] = es;
	}

	if (toCompact > nonCompactStateCount) {
		toCompact = nonCompactStateCount / 2;
	}

	std::partial_sort(
		arr.begin(),
		arr.begin() + toCompact,
		arr.end(),
		KillOrCompactOrdering());

	for (i = 0; i < toCompact; ++i)
		compactState(arr[i]);

	onlyNonCompact = false;
}

void ExeStateManager::compactState(ExecutionState* s)
{
	assert (s->isCompact() == false);

	s->compact();
	nonCompactStateCount--;

	std::cerr << "COMPACTING:: s=" << (void*)s << ". compact=" << (void*)
		s << ((s->isReplayDone()) ? ". NOREPLAY\n": ". INREPLAY\n");
	assert (s->isCompact());
}

void ExeStateManager::compactPressureStates(uint64_t maxMem)
{
	// compact instead of killing
	// (a rough measure)
	unsigned s = nonCompactStateCount + ((size()-nonCompactStateCount)/16);
	uint64_t mbs = getMemUsageMB();
	unsigned toCompact = std::max((uint64_t)1, (uint64_t)s - s*maxMem/mbs);

	toCompact = std::min(toCompact, (unsigned) nonCompactStateCount);
	klee_warning("compacting %u states (over memory cap)", toCompact);

	compactStates(toCompact);

	onlyNonCompact = true;
}

Searcher::States ExeStateManager::getStates(void) const
{
	if (yieldStates.size()) {
		allRemovedStates = yieldStates;
		allRemovedStates.insert(
			removedStates.begin(), removedStates.begin());
		return Searcher::States(addedStates, allRemovedStates);
	}

	return Searcher::States(addedStates, removedStates);
}

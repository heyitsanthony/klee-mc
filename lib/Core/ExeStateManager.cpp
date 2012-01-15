#include "Executor.h"
#include "ExeStateManager.h"
#include "Searcher.h"
#include "UserSearcher.h"
#include "MemUsage.h"
#include "klee/Common.h"
#include "static/Sugar.h"
#include "PTree.h"

#include <llvm/Support/CommandLine.h>

#include <algorithm>
#include <iostream>

using namespace llvm;
using namespace klee;

ExeStateManager::ExeStateManager()
: nonCompactStateCount(0)
, searcher(NULL)
{}

ExeStateManager::~ExeStateManager()
{
	if (searcher)
		delete searcher;
}

ExecutionState* ExeStateManager::selectState(bool allowCompact)
{
	ExecutionState* ret;

	assert (!empty());

	/* only yielded states left? well.. pop one */
	if (states.empty()) {
		ExecutionState	*es;

		assert (addedStates.empty());
		assert (!yieldedStates.empty());

		es = *yieldedStates.begin();

		yieldedStates.erase(es);
		queueAdd(es);
		searcher->update(NULL, getStates());
		states.insert(es);
		addedStates.clear();
	}

	ret = &searcher->selectState(allowCompact);

	assert (ret->checkCanary());
	assert((allowCompact || !ret->isCompact()) && "compact state chosen");

	return ret;
}

void ExeStateManager::setupSearcher(Executor* exe)
{
	assert (!searcher && "Searcher already inited");
	searcher = UserSearcher::constructUserSearcher(*exe);
	searcher->update(
		NULL,
		Searcher::States(states, Searcher::States::emptySet));
}

void ExeStateManager::teardownUserSearcher(void)
{
	assert (searcher);
	delete searcher;
	searcher = NULL;
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
		commitQueue(exe, NULL); /* XXX ??? */
	} else {
		states.insert(initialState);
		nonCompactStateCount++;
	}
}

void ExeStateManager::setWeights(double weight)
{
	foreach (it, states.begin(), states.end())
		(*it)->weight = weight;
}

void ExeStateManager::queueAdd(ExecutionState* es) {
assert (es->checkCanary());
addedStates.insert(es); }

void ExeStateManager::queueRemove(ExecutionState* s) { removedStates.insert(s); }

/* note: only a state that has already been added can call yield--
 * this means 's' is gauranteed to be the states list and not in addedStates */
void ExeStateManager::yield(ExecutionState* s)
{
	ExecutionState	*compacted;

	/* do not yield if we're low on states */
	if (states.size() == 1)
		return;

	assert (!s->isCompact() && "yielding compact state? HOW?");

	compacted = compactState(s);

	/* compact state forced a replace copy,
	 * new state is put on addedStates-- So take it away.
	 * old state is put on removedStates-- OK. */
	dropAdded(compacted);

	yieldedStates.insert(compacted);
	/* NOTE: states and yieldedStates are disjoint */
}

void ExeStateManager::dropAdded(ExecutionState* es)
{
	ExeStateSet::iterator it = addedStates.find(es);
	assert (it != addedStates.end());
	addedStates.erase(it);
}

void ExeStateManager::commitQueue(Executor* exe, ExecutionState *current)
{
	ExecutionState	*root_to_be_removed;

	root_to_be_removed = NULL;

	if (searcher != NULL)
		searcher->update(current, getStates());

	foreach (it, removedStates.begin(), removedStates.end()) {
		ExecutionState *es = *it;

		ExeStateSet::iterator it2 = states.find(es);
		assert (it2 != states.end());
		states.erase(it2);

		if (!es->isCompact())
			--nonCompactStateCount;

		// delete es
		exe->removePTreeState(es, &root_to_be_removed);
	}

	if (root_to_be_removed)
		exe->removeRoot(root_to_be_removed);

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

void ExeStateManager::compactStates(
	ExecutionState* &state, unsigned toCompact)
{
	std::vector<ExecutionState*> arr(nonCompactStateCount);
	unsigned i = 0;

	foreach (si, states.begin(), states.end()) {
		if ((*si)->isCompact())
			continue;
		arr[i++] = *si;
	}

	if (toCompact > nonCompactStateCount) {
		toCompact = nonCompactStateCount / 2;
	}

	std::partial_sort(
		arr.begin(),
		arr.begin() + toCompact,
		arr.end(),
		KillOrCompactOrdering());

	for (i = 0; i < toCompact; ++i) {
		ExecutionState* compacted;

		compacted = compactState(arr[i]);
		if (state == arr[i])
			state = compacted;
	}
}

ExecutionState* ExeStateManager::compactState(ExecutionState* s)
{
	ExecutionState* compacted;

	assert (s->isCompact() == false);

	compacted = s->compact();
	compacted->coveredNew = false;
	compacted->ptreeNode = s->ptreeNode;

	std::cerr << "COMPACTING:: s=" << (void*)s << ". compact=" << (void*)
		compacted
		<< ((s->isReplayDone())
		? ". NOREPLAY\n"
		: ". INREPLAY\n");


	replaceState(s, compacted);

	return compacted;
}

void ExeStateManager::compactPressureStates(
	ExecutionState* &state, uint64_t maxMem)
{
	// compact instead of killing
	// (a rough measure)
	unsigned s = nonCompactStateCount + ((size()-nonCompactStateCount)/16);
	uint64_t mbs = getMemUsageMB();
	unsigned toCompact = std::max((uint64_t)1, (uint64_t)s - s*maxMem/mbs);

	toCompact = std::min(toCompact, (unsigned) nonCompactStateCount);
	klee_warning("compacting %u states (over memory cap)", toCompact);

	compactStates(state, toCompact);
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

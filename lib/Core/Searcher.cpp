//===-- Searcher.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Searcher.h"
#include "Executor.h"
#include "ExeStateManager.h"

#include "klee/Internal/Module/KModule.h"
#include "BumpingMergeSearcher.h"
#include "DFSSearcher.h"

#include "static/Sugar.h"

using namespace klee;

const std::set<ExecutionState*> Searcher::States::emptySet;

ExecutionState &DFSSearcher::selectState(bool allowCompact)
{
	foreach (i, states.rbegin(), states.rend()) {
		ExecutionState* es = *i;

		if (!allowCompact && es->isCompact())
			continue;

		return *es;
	}

	// no non-compact [if !allowCompact]) states remain
	return *states.back();
}

void DFSSearcher::update(ExecutionState *current, const States s)
{
	unsigned removed_c;

	states.insert(states.end(), s.getAdded().begin(), s.getAdded().end());

	if (s.getRemoved().empty())
		return;

	removed_c = 0;

	/* hack for common case of removing only one state...
	* no need to scan the entire state list */
	if (s.getRemoved().count(states.back())) {
		ExecutionState	*x = states.back();
		states.pop_back();
		removed_c++;
		if (s.getRemoved().size() == removed_c) {
			return;
		}
	}

	for (	std::list<ExecutionState*>::iterator it = states.begin(),
		ie = states.end(); it != ie;)
	{
		ExecutionState* es = *it;
		if (s.getRemoved().count(es)) {
			it = states.erase(it);
			removed_c++;
			if (removed_c == s.getRemoved().size())
				break;
		} else {
			++it;
		}
	}
}

BumpMergingSearcher::BumpMergingSearcher(
	ExecutorBC &_executor, Searcher *_baseSearcher)
: executor(_executor)
, baseSearcher(_baseSearcher)
, mergeFunction(executor.getKModule()->kleeMergeFn)
{}

BumpMergingSearcher::~BumpMergingSearcher() { delete baseSearcher; }

Searcher::Searcher() {}
Searcher::~Searcher() {}

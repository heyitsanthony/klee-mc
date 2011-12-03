#include <assert.h>
#include "static/Sugar.h"
#include "klee/ExecutionState.h"
#include "PrioritySearcher.h"
#include <iostream>

using namespace klee;

ExecutionState& PrioritySearcher::selectState(bool allowCompact)
{
	PrStates*	prs;
	ExecutionState*	next;
	
	prs = pr_heap.top();
	assert (prs->getStateCount());

	next = prs->getNext();
	assert (next != NULL);

	return *next;
}

void PrioritySearcher::update(ExecutionState *current, States s)
{
	/* new states */
	foreach (it, s.getAdded().begin(), s.getAdded().end()) {
		ExecutionState	*new_st(*it);
		PrStates	*prs;
		int		pr;
		unsigned	idx;
		
		pr = prFunc->getPriority(*new_st);
		prs = getPrStates(pr);
		idx = prs->addState(new_st);
		state_backmap[new_st] = stateidx_ty(pr, idx);
		state_c++;
	}

	/* update current */
	if (current != NULL) {
		int			new_pr;
		statemap_ty::iterator	sm_it(state_backmap.find(current));

		new_pr = prFunc->getPriority(*current);
		if ((sm_it->second).first != new_pr) {
			PrStates	*prs;
			unsigned	idx;

			prs = getPrStates((sm_it->second).first);
			prs->rmvState((sm_it->second).second);

			prs = getPrStates(new_pr);
			idx = prs->addState(current);
			state_backmap[current] = stateidx_ty(new_pr, idx);
		}
		
	}

	/* removed states */
	foreach (it, s.getRemoved().begin(), s.getRemoved().end()) {
		statemap_ty::iterator	sm_it(state_backmap.find(*it));
		stateidx_ty		stateidx;
		PrStates		*prs;

		assert (sm_it != state_backmap.end());
		
		stateidx = sm_it->second;
		prs = getPrStates(stateidx.first);
		prs->rmvState(stateidx.second);
		state_backmap.erase(sm_it->first);

		state_c--;
	}

	/* clear out dead priorities */
	while (!pr_heap.empty()) {
		PrStates	*prs;

		prs = pr_heap.top();
		if (prs->getStateCount())
			break;

		pr_heap.pop();
		priorities.erase(prs->getPr());
		delete prs;
	}
}

PrioritySearcher::PrStates* PrioritySearcher::getPrStates(int n)
{
	prmap_ty::iterator	it(priorities.find(n));
	PrStates		*prs;

	if (it != priorities.end())
		return it->second;

	prs = new PrStates(n);
	priorities[n] = prs;
	pr_heap.push(prs);

	return prs;
}

unsigned PrioritySearcher::PrStates::addState(ExecutionState* es)
{
	if (used_states == states.size()) {
		states.push_back(es);
		used_states++;
		return states.size() - 1;
	}

	for (unsigned k = 0; k < states.size(); k++) {
		if (states[k] == NULL) {
			used_states++;
			states[k] = es;
			return k;
		}
	}

	assert (0 == 1 && "OOPS");
	return 0;
}

void PrioritySearcher::PrStates::rmvState(unsigned idx)
{
	int	tail;

	assert (states.size() > idx);
	assert (states[idx] != NULL);

	states[idx] = NULL;
	used_states--;

	if (idx < (states.size() - 1))
		return;
	
	tail = 0;
	for (int k = states.size() - 1; k >= 0; k--) {
		if (states[k] != NULL)
			break;
		tail++;
	}

	assert (tail > 0);

	states.resize(states.size() - tail);
}

ExecutionState* PrioritySearcher::PrStates::getNext(void)
{
	unsigned	sz;
	assert (used_states != 0);

	sz = states.size();
	for (unsigned k = next_state; k < sz; k++) {
		if (states[k]) {
			next_state = k+1;
			return states[k];
		}
	}

	for (unsigned k = 0; k < sz; k++) {
		if (states[k]) {
			next_state = k+1;
			return states[k];
		}
	}

	return NULL;
}

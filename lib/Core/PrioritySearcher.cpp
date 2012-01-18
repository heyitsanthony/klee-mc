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
	unsigned	refresh_c, max_refresh;

	refresh_c = 0;
	max_refresh = 1;

	while (1) {
		int	curPr;

		prs = pr_heap.top();

		if (prs->getStateCount() == 0) {
			clearDeadPriorities();
			continue;
		}

		next = prs->getNext();
		prFunc->latch();
		curPr = prFunc->getPriority(*next);
		prFunc->unlatch();


		if (refresh_c < max_refresh && curPr != prs->getPr()) {
			prFunc->latch();
			// refreshPriority(next);
			int demote_pr = (curPr - prs->getPr())/2 + prs->getPr() - 1;
			if (demote_pr != prs->getPr())
				demote(next, demote_pr);

			std::cerr << "PR: " <<
				prs->getPr() << " -> " << curPr << '\n';
			prFunc->unlatch();
			refresh_c++;
			continue;
		}

		std::cerr << "SELSTATE="
		 << next->pc->getInst()->getParent()->getParent()->getNameStr()
		 << (next->isReplayDone() ? ". NOREPLAY\n" : ". INREPLAY\n");
		std::cerr
			<< "CURRENT PR=" << prs->getPr()
			<< ". COUNT=" << prs->getStateCount() << "\n";
		std::cerr
			<< "PRIORITY: GOT= " << curPr
			<< ". EXPECTED=" << prs->getPr() <<". FIXED="
			<< refresh_c
			<< ".\n";
		break;

	}
	assert (next != NULL);

	///* penalize out-going state */
	prFunc->getPriority(*next);
	demote(next, prs->getPr() - 1);

	return *next;
}

void PrioritySearcher::demote(ExecutionState* es, int new_pr)
{
	PrStates		*prs;
	unsigned		idx;
	statemap_ty::iterator	sm_it(state_backmap.find(es));

	assert ((sm_it->second).first != new_pr);

	prs = getPrStates((sm_it->second).first);
	prs->rmvState((sm_it->second).second);

	prs = getPrStates(new_pr);
	idx = prs->addState(es);
	state_backmap[es] = stateidx_ty(new_pr, idx);
}


bool PrioritySearcher::refreshPriority(ExecutionState* es)
{
	PrStates		*prs;
	unsigned		idx;
	int			new_pr;
	statemap_ty::iterator	sm_it(state_backmap.find(es));

	new_pr = prFunc->getPriority(*es);
	if ((sm_it->second).first == new_pr)
		return false;


	prs = getPrStates((sm_it->second).first);
	prs->rmvState((sm_it->second).second);

	prs = getPrStates(new_pr);
	idx = prs->addState(es);
	state_backmap[es] = stateidx_ty(new_pr, idx);

	return true;
}

void PrioritySearcher::update(ExecutionState *current, States s)
{
	/* new states */
	foreach (it, s.getAdded().begin(), s.getAdded().end())
		addState(*it);

	/* update current */
	if (current != NULL)
		refreshPriority(current);

	/* removed states */
	foreach (it, s.getRemoved().begin(), s.getRemoved().end())
		removeState(*it);

	clearDeadPriorities();
}

void PrioritySearcher::addState(ExecutionState* es)
{
	PrStates	*prs;
	int		pr;
	unsigned	idx;

	pr = prFunc->getPriority(*es);
	std::cerr << "ADDING="
		 << es->pc->getInst()->getParent()->getParent()->getNameStr()
		 << " to PR=" << pr << '\n';

	prs = getPrStates(pr);
	idx = prs->addState(es);
	state_backmap[es] = stateidx_ty(pr, idx);


	state_c++;
}

void PrioritySearcher::removeState(ExecutionState* es)
{
	statemap_ty::iterator	sm_it(state_backmap.find(es));
	stateidx_ty		stateidx;
	PrStates		*prs;

	assert (sm_it != state_backmap.end());
	
	std::cerr << "RMV="
		 << es->pc->getInst()->getParent()->getParent()->getNameStr()
		 << " FROM PR=" << sm_it->second.first << '\n';

	stateidx = sm_it->second;
	prs = getPrStates(stateidx.first);

	prs->rmvState(stateidx.second);
	state_backmap.erase(sm_it);

	state_c--;
}

void PrioritySearcher::clearDeadPriorities(void)
{
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
	next_state = rand() % sz;

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

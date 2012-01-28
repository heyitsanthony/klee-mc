#include <assert.h>
#include "static/Sugar.h"
#include "klee/ExecutionState.h"
#include "PrioritySearcher.h"
#include <iostream>
#include "llvm/Support/CommandLine.h"


using namespace klee;

namespace {
	llvm::cl::opt<unsigned>
	PrKickRate(
		"pr-kick-rate",
		llvm::cl::desc("Number of elements to kick per selection."),
		llvm::cl::init(1));
}

ExecutionState& PrioritySearcher::selectState(bool allowCompact)
{
	prsearcher_ty	prs;
	ExecutionState*	next;
	unsigned	refresh_c, max_refresh;

	refresh_c = 0;
	max_refresh = PrKickRate;

	while (1) {
		int	curPr;
		int	heap_pr;


		prs = pr_heap.top();
		heap_pr = prs.first;

		if (prs.second->empty()) {
			clearDeadPriorities();
			continue;
		}

		next = &prs.second->selectState(allowCompact);
		prFunc->latch();
		curPr = prFunc->getPriority(*next);
		prFunc->unlatch();


		if (refresh_c < max_refresh && curPr != heap_pr) {
			prFunc->latch();
			//refreshPriority(next);
			int demote_pr = (curPr - heap_pr)/2 + heap_pr - 1;
			if (demote_pr != heap_pr)
				demote(next, demote_pr);

			std::cerr << "PR: " << heap_pr << " -> " << curPr << '\n';
			prFunc->unlatch();
			refresh_c++;
			continue;
		}

		std::cerr << "SELSTATE="
		 << next->pc->getInst()->getParent()->getParent()->getNameStr()
		 << (next->isReplayDone() ? ". NOREPLAY\n" : ". INREPLAY\n");
		std::cerr
			<< "CURRENT PR=" << heap_pr  << ". COUNT=???\n";
		std::cerr
			<< "PRIORITY: GOT= " << curPr
			<< ". EXPECTED=" << heap_pr <<". FIXED="
			<< refresh_c
			<< ".\n";
		break;

	}
	assert (next != NULL);

	///* penalize out-going state */
	prFunc->getPriority(*next);
	demote(next, prs.first - 1);

	return *next;
}

void PrioritySearcher::demote(ExecutionState* es, int new_pr)
{
	Searcher		*prs;
	statemap_ty::iterator	sm_it(state_backmap.find(es));

	assert (sm_it->second != new_pr);

	prs = getPrSearcher(sm_it->second);
	prs->removeState(es);

	prs = getPrSearcher(new_pr);
	prs->addState(es);
	state_backmap[es] = new_pr;
}


bool PrioritySearcher::refreshPriority(ExecutionState* es)
{
	Searcher		*prs;
	int			new_pr;
	statemap_ty::iterator	sm_it(state_backmap.find(es));

	new_pr = prFunc->getPriority(*es);
	if (sm_it->second == new_pr)
		return false;

	prs = getPrSearcher(sm_it->second);
	prs->removeState(es);

	prs = getPrSearcher(new_pr);
	prs->addState(es);
	state_backmap[es] = new_pr;

	return true;
}

void PrioritySearcher::update(ExecutionState *current, States s)
{
	/* new states */
	foreach (it, s.getAdded().begin(), s.getAdded().end())
		addState(*it);

	/* removed states */
	foreach (it, s.getRemoved().begin(), s.getRemoved().end()) {
		ExecutionState	*es = *it;
		removeState(es);
		if (es == current)
			current = NULL;
	}

	/* update current */
	if (current != NULL) {
		prFunc->latch();
		refreshPriority(current);
		prFunc->unlatch();
	}

	clearDeadPriorities();
}

void PrioritySearcher::addState(ExecutionState* es)
{
	Searcher	*prs;
	int		pr;

	pr = prFunc->getPriority(*es);

	prs = getPrSearcher(pr);
	prs->addState(es);
	state_backmap[es] = pr;

	std::cerr << "ADD ES=" << (void*)es << " TO PR=" << pr << '\n';
	state_c++;
}

void PrioritySearcher::removeState(ExecutionState* es)
{
	statemap_ty::iterator	sm_it(state_backmap.find(es));
	Searcher		*prs;
	int			pr;

	assert (sm_it != state_backmap.end());

	pr = sm_it->second;
	prs = getPrSearcher(sm_it->second);

	prs->removeState(es);
	state_backmap.erase(sm_it);

	std::cerr << "RMV ES=" << (void*)es << " FROM PR=" << pr << '\n';
	state_c--;
}

void PrioritySearcher::clearDeadPriorities(void)
{
	/* clear out dead priorities */
	while (!pr_heap.empty()) {
		prsearcher_ty	prs;

		prs = pr_heap.top();
		if (!prs.second->empty())
			break;

		pr_heap.pop();
		priorities.erase(prs.first);
		delete prs.second;
	}
}

Searcher* PrioritySearcher::getPrSearcher(int n)
{
	Searcher		*new_searcher;
	prmap_ty::iterator	it(priorities.find(n));
	prsearcher_ty		prs;

	if (it != priorities.end())
		return (it->second).second;

	new_searcher = searcher_base->createEmpty();
	prs = prsearcher_ty(n, new_searcher);

	priorities[n] = prs;
	pr_heap.push(prs);

	return new_searcher;
}

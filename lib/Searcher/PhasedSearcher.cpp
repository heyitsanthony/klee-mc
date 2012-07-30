#include <assert.h>
#include <klee/ExecutionState.h>
#include <static/Sugar.h>

#include "PhasedSearcher.h"

using namespace klee;

ExecutionState& PhasedSearcher::selectState(bool allowCompact)
{
	ExecutionState	*ret;

	do {
		cur_phase++;
		if (cur_phase >= phases.size())
			cur_phase = 0;

		if (phases[cur_phase].empty())
			continue;

		/* steal state from top of phase list,
		 * kick to back of list */
		ret = phases[cur_phase].front();
		phases[cur_phase].pop_front();
		phases[cur_phase].push_back(ret);
		break;
	} while (1);

	return *ret;
}

void PhasedSearcher::update(ExecutionState *current, States s)
{
	foreach (it, s.getAdded().begin(), s.getAdded().end()) {
		ExecutionState*	es(*it);
		unsigned	last_br;

		last_br = es->branchLast().first;
		if (last_br >= phases.size())
			phases.resize(last_br+1);

		phases[last_br].push_front(es);
		state_backmap[es] = last_br;
		state_c++;
	}

	foreach (it, s.getRemoved().begin(), s.getRemoved().end()) {
		ExecutionState		*es(*it);
		backmap_ty::iterator	it2(state_backmap.find(es));
		unsigned		list_br;

		assert (it2 != state_backmap.end());
		list_br = it2->second;

		state_backmap.erase(it2);

		/* fastpath-- current state destroyed */
		if (phases[list_br].back() == es) {
			phases[list_br].pop_back();
			state_c--;
			continue;
		}

		/* slowpath */
		foreach (it3, phases[list_br].begin(), phases[list_br].end()) {
			ExecutionState	*q_es(*it3);

			if (q_es == es) {
				phases[list_br].erase(it3);
				break;
			}
		}

		state_c--;
	}

	updateCurrent(current);
}

void PhasedSearcher::updateCurrent(ExecutionState* current)
{
	backmap_ty::iterator	cur_it(state_backmap.find(current));
	unsigned		cur_br;

	if (cur_it == state_backmap.end())
		return;

	cur_br = current->branchLast().first;

	/* no need to change lists if same branch */
	if (cur_br == cur_it->second)
		return;

	/* current state should always be on tail of list */
	assert (phases[cur_it->second].back() == current);
	phases[cur_it->second].pop_back();

	if (cur_br >= phases.size())
		phases.resize(cur_br+1);

	phases[cur_br].push_back(current);
	state_backmap[current] = cur_br;
}

#include <assert.h>
#include "klee/Internal/ADT/RNG.h"
#include "static/Sugar.h"
#include "RescanSearcher.h"

using namespace klee;
namespace klee { extern RNG theRNG; }

ExecutionState& RescanSearcher::selectState(bool allowCompact)
{
	ExecutionState			*es;
	std::vector<ExecutionState*>	matches;
	int				max_pr;

	assert (state_c > 0);

	pr->latch();
	es = states.front();
	max_pr = pr->getPriority(*es);
	foreach (it, states.begin(), states.end()) {
		ExecutionState	*cur_es = *it;
		int		cur_pr = pr->getPriority(*cur_es);

		if (cur_pr > max_pr) {
			max_pr = cur_pr;
			matches.clear();
			matches.push_back(cur_es);
		} else if (cur_pr == max_pr) {
			matches.push_back(cur_es);
		}
	}

	pr->unlatch();

	es = matches[theRNG.getInt32() % matches.size()];

	return *es;
}

void RescanSearcher::update(ExecutionState *current, States s)
{
	unsigned	removed_c;

	foreach (it, s.getAdded().begin(), s.getAdded().end()) {
		states.push_back(*it);
		state_c++;
	}

	if (s.getRemoved().empty())
		return;

	removed_c = 0;
	for (	state_list_ty::iterator it = states.begin(), ie = states.end();
		it != ie;)
	{
		if (s.getRemoved().count(*it)) {
			state_list_ty::iterator old_it = it;
			it++;
			states.erase(old_it);
			state_c--;
			removed_c++;
		} else
			it++;
	}

	assert (removed_c == s.getRemoved().size());
}

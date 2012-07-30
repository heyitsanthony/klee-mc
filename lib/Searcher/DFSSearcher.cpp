#include "DFSSearcher.h"
#include "klee/ExecutionState.h"
#include "static/Sugar.h"

using namespace klee;

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



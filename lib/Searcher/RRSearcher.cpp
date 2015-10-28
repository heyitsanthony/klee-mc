#include "../Core/Searcher.h"

#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KModule.h"
#include "RRSearcher.h"

#include "static/Sugar.h"

using namespace klee;
using namespace llvm;

ExecutionState* RRSearcher::selectState(bool allowCompact)
{
	while (cur_state != states.end()) {
		ExecutionState* es = *cur_state;
		cur_state++;
		if (!allowCompact && es->isCompact()) continue;
		return es;
	}

	cur_state = states.begin();

	if (states.empty()) {
		return nullptr;
	}

	// no non-compact [if !allowCompact]) states remain
	return states.back();
}

void RRSearcher::update(ExecutionState *current, const States s)
{
	states.insert(states.end(), s.getAdded().begin(), s.getAdded().end());

	if (cur_state == states.end() && states.empty() == false)
		cur_state = states.begin();

	if (s.getRemoved().empty()) return;

	/* hack for common case of removing only one state...
	* no need to scan the entire state list */
	if (s.getRemoved().count(states.back())) {
		if (*cur_state == states.back())
			cur_state = states.begin();
		states.pop_back();

		if (states.begin() == states.end())
			cur_state = states.end();

		if (s.getRemoved().size() == 1)
			return;
	}

	bool reset_cur = false;
	for (	std::list<ExecutionState*>::iterator it = states.begin(),
		ie = states.end(); it != ie;)
	{
		ExecutionState* es = *it;
		if (s.getRemoved().count(es) == 0) {
			++it;
			continue;
		}
		if (it == cur_state)
			reset_cur = true;
		it = states.erase(it);
		if (reset_cur) cur_state = it;
	}

	if (cur_state == states.end())
		cur_state = states.begin();
}

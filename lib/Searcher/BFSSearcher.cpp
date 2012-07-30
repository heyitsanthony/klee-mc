#include "BFSSearcher.h"
#include "../Core/Executor.h"
#include "static/Sugar.h"

using namespace klee;

ExecutionState &BFSSearcher::selectState(bool allowCompact)
{
	foreach (it, states.begin(), states.end()) {
		ExecutionState* es = *it;
		if (!allowCompact && es->isCompact())
			continue;

		// requeue the state at the back of the queue
		// equiv to pop_front, in the absence of compact states
		states.erase(it);
		states.push_back(es);
		return *es;
	}

	// no non-compact [if !allowCompact]) states remain
	return *states.front();
}

void BFSSearcher::update(ExecutionState *current, const States s)
{
	states.insert(states.end(), s.getAdded().begin(), s.getAdded().end());

	foreach (it, s.getRemoved().begin(), s.getRemoved().end()) {
		ExecutionState *es = *it;

		/* cheap check */
		if (es == states.back()) {
			states.pop_back();
			continue;
		}

		/* full check */
		bool ok = false;
		foreach (it, states.begin(), states.end()) {
			if (es == *it) {
				states.erase(it);
				ok = true;
				break;
			}
		}
		assert(ok && "invalid state removed");
	}
}

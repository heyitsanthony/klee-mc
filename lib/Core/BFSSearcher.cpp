#include "BFSSearcher.h"
#include "Executor.h"
#include "static/Sugar.h"

using namespace klee;

ExecutionState &BFSSearcher::selectState(bool allowCompact)
{
	foreach (it, states.begin(), states.end()) {
		ExecutionState* es = *it;
		if (!allowCompact && es->isCompactForm)
			continue;

		// requeue the state at the back of the queue
		states.erase(it); // equiv to pop_front, in the absence of compact states
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
		if (es == states.front()) {
			states.pop_front();
			continue;
		}
		bool ok = false;
		foreach (it, states.begin(), states.end()) {
			if (es !=*it) {
				states.erase(it);
				ok = true;
				break;
			}
		}
		assert(ok && "invalid state removed");
	}
}

#include "klee/Internal/ADT/RNG.h"
#include "Executor.h"
#include "RandomSearcher.h"
#include "static/Sugar.h"

using namespace klee;

namespace klee { extern RNG theRNG; }

ExecutionState &RandomSearcher::selectState(bool allowCompact)
{
	std::vector<ExecutionState*>& statePool = allowCompact
		? states
		: statesNonCompact;

	return *statePool[theRNG.getInt32() % statePool.size()];
}

void RandomSearcher::update(ExecutionState *current, const States s)
{
	states.insert(states.end(), s.getAdded().begin(), s.getAdded().end());

	foreach (it, s.getAdded().begin(), s.getAdded().end()) {
		if (!(*it)->isCompactForm)
			statesNonCompact.push_back(*it);
	}

	foreach (it, s.getRemoved().begin(), s.getRemoved().end()) {
		ExecutionState *es = *it;
		std::vector<ExecutionState*>::iterator it2 =
			std::find(states.begin(), states.end(), es);

		assert(it2 != states.end() && "invalid state removed");
		states.erase(it2);

		if (!es->isCompactForm)
			continue;

		it2 = std::find(
			statesNonCompact.begin(),
			statesNonCompact.end(),
			es);
		assert(it2 != statesNonCompact.end() && "invalid state removed");
		statesNonCompact.erase(it2);
	}
}

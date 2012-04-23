#include "OverrideSearcher.h"

using namespace klee;

ExecutionState*	OverrideSearcher::override_es = NULL;

void OverrideSearcher::update(ExecutionState *current, States s)
{
	if (override_es && s.getRemoved().count(override_es))
		override_es = NULL;

	searcher_base->update(current, s);
}

ExecutionState& OverrideSearcher::selectState(bool allowCompact)
{
	if (override_es != NULL)
		return *override_es;

	return searcher_base->selectState(allowCompact);
}

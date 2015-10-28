#include "StickySearcher.h"
#include "klee/ExecutionState.h"
#include "static/Sugar.h"

using namespace klee;

ExecutionState* StickySearcher::selectState(bool allowCompact)
{
	ExecutionState	*new_es;

	if (sticky_st != NULL) return sticky_st;

	new_es = base->selectState(allowCompact);
	if (!new_es) return nullptr;
	if (new_es->newInsts) sticky_st = new_es;
	return new_es;
}

void StickySearcher::update(ExecutionState *current, const States s)
{
	if (s.getRemoved().count(sticky_st))
		sticky_st = NULL;

	base->update(current, s);
}

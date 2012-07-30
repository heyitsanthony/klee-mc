#include "../Core/Executor.h"
#include "../Core/CoreStats.h"
#include "ConcretizingSearcher.h"

using namespace klee;

#define TOTAL_INS	\
	stats::coveredInstructions + stats::uncoveredInstructions

void ConcretizingSearcher::update(ExecutionState *current, States s)
{
	if (current != last_es)
		last_es = NULL;

	searcher_base->update(current, s);

	if (current != last_es || current == NULL)
		return;

	if (s.getRemoved().count(current)) {
		last_es = NULL;
		return;
	}

	if (last_cov < TOTAL_INS)
		last_es = current;
}

ExecutionState& ConcretizingSearcher::selectState(bool allowCompact)
{
	if (last_es && last_cov < TOTAL_INS) {
		last_cov = TOTAL_INS;
		if (last_es->isConcrete() == false) {
			exe.concretizeState(*last_es);
			concretized = true;
		}
	}

	last_cov = TOTAL_INS;

	if (concretized && last_es != NULL) {
		concretized = false;
		return *last_es;
	}

	concretized = false;
	last_es = &searcher_base->selectState(allowCompact);
	return *last_es;
}

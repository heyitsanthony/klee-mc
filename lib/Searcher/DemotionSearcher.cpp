#include "static/Sugar.h"
#include "DemotionSearcher.h"
#include "../Core/CoreStats.h"

using namespace klee;

DemotionSearcher::DemotionSearcher(
	Searcher* _searcher_base,
	unsigned _max_repeats)
: searcher_base(_searcher_base)
, last_current(0)
, repeat_c(0)
, max_repeats(_max_repeats)
, last_ins_uncov(0)
{}

ExecutionState *DemotionSearcher::selectState(bool allowCompact)
{
	ExecutionState	*es;

	assert (searcher_base->empty() == false);

	es = searcher_base->selectState(allowCompact);

	/* new state? */
	if (es != last_current) {
		repeat_c = 0;
		last_ins_uncov = stats::uncoveredInstructions;
		last_current = es;
		return es;
	}

	/* repeat state... */
	std::cerr << "[Demote] STATE REPEATING: REPEAT_C=" << repeat_c << '\n';

	if (last_ins_uncov < stats::uncoveredInstructions) {
		/* if the state uncovered new instructions, then it's useful
		 * and we should reset its repeat penalty */
		repeat_c = 0;
		last_ins_uncov = stats::uncoveredInstructions;
		return es;
	}

	/* incur penalty */
	repeat_c++;
	if (repeat_c <= max_repeats)
		return es;

	/* exceeded repeat count, revoke! */
	repeat_c = 0;
	last_current = NULL;
	searcher_base->removeState(es);
	scheduled.erase(es);
	demoted.insert(es);

	if (scheduled.empty()) {
		std::cerr << "RECOVERING DEMOTED\n";
		recoverDemoted();
	}

	return selectState(allowCompact);
}

void DemotionSearcher::recoverDemoted(void)
{
	ExecutionState		*recovered_state;
	ExeStateSet::iterator	it;

	assert (scheduled.empty());

	if (demoted.empty())
		return;

	it = demoted.begin();
	recovered_state = *it;

	searcher_base->addState(recovered_state);
	scheduled.insert(recovered_state);
	demoted.erase(it);
}

void DemotionSearcher::update(ExecutionState *current, States s)
{
	ExeStateSet	rmvSet;

	/* filter out removals since it may be in the demotion */
	foreach (it, s.getRemoved().begin(), s.getRemoved().end()) {
		ExecutionState	*es = *it;

		if (scheduled.count(es))
			rmvSet.insert(es);
		else
			demoted.erase(es);
	}


	scheduled.insert(s.getAdded().begin(), s.getAdded().end());
	foreach (it, rmvSet.begin(), rmvSet.end())
		scheduled.erase(*it);

	searcher_base->update(current, States(s.getAdded(), rmvSet));

	/* pull a state from the black list if nothing left to schedule */
	if (searcher_base->empty()) {
		recoverDemoted();
	}
}

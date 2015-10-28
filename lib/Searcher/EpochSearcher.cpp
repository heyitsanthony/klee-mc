#include <iostream>
#include <assert.h>
#include <algorithm>

#include "static/Sugar.h"
#include "../Core/CoreStats.h"
#include "../Core/Executor.h"
#include "klee/Internal/ADT/RNG.h"

#include "EpochSearcher.h"

using namespace klee;

namespace klee { extern RNG theRNG; }

#define CONCRETE_WATERMARK	10
#define INST_TOTAL (stats::coveredInstructions + stats::uncoveredInstructions)

EpochSearcher::EpochSearcher(
	Executor& _exe,
	Searcher* _searcher_base, Searcher* _global_pool)
: searcher_base(_searcher_base)
, global_pool(_global_pool)
, last_cov(INST_TOTAL)
, pool_countdown(3)
, pool_period(3)
, epoch_state_c(0)
, exe(_exe)
{
	epochs.emplace_back(searcher_base->createEmpty());
}


ExecutionState* EpochSearcher::selectPool(bool allowCompact)
{
	auto es = global_pool->selectState(allowCompact);

	/* already have it in an epoch; the global pool has excellent taste */
	if (es2epoch.find(es) != es2epoch.end())
		return es;

	/* insert into epochs */
	epochs.back().add(es);
	es2epoch.insert(std::make_pair(es, epochs.size()-1));
	return es;
}

ExecutionState* EpochSearcher::selectEpoch(bool allowCompact)
{
	unsigned	e_idx = theRNG.getInt31() % epochs.size();

	if (epochs[e_idx].getSearcher()->empty()) {
		std::cerr << "[Epoch] Empty epoch. Pull from pool.\n";
		return selectPool(allowCompact);
	}

	return epochs[e_idx].getSearcher()->selectState(allowCompact);
}

ExecutionState* EpochSearcher::selectState(bool allowCompact)
{
	ExecutionState	*next_state;

	if (INST_TOTAL > last_cov) {
		pool_countdown = pool_period;
		last_cov = INST_TOTAL;
	} else
		pool_countdown--;

	std::cerr << "[Epoch] POOL COUNTDOWN=" << pool_countdown << '\n';
	if (pool_countdown == 0) {
		/* select from pool */
		pool_countdown = pool_period;
		std::cerr << "[Epoch] SELECT FROM GLOBAL POOL\n";
		return selectPool(allowCompact);
	}

	/* select from epochs */
	std::cerr	<< "[Epoch] SELECT FROM EPOCH (states="
			<< epoch_state_c << ")\n";

	next_state = selectEpoch(allowCompact);
	if (epoch_state_c > CONCRETE_WATERMARK) {
		static int	con_backoff = 0;

		if (con_backoff == 0) {
			std::cerr << "[Epoch] Concretizing state\n";
			pool_countdown++;
			exe.concretizeState(*next_state);
		}

		con_backoff = (con_backoff+1) % pool_period;
	}

	return next_state;
}


void EpochSearcher::Epoch::add(ExecutionState* s)
{
	assert (states.count(s) == 0);
	states.insert(s);
	searcher->addState(s);
}

void EpochSearcher::Epoch::update(
	ExecutionState* current,
	const Searcher::States& s)
{
	std::set<ExecutionState*>	add, rmv;
	std::set_intersection(
		s.getAdded().begin(), s.getAdded().end(),
		states.begin(), states.end(),
		std::inserter(add, add.begin()));

	std::set_intersection(
		s.getRemoved().begin(), s.getRemoved().end(),
		states.begin(), states.end(),
		std::inserter(rmv, rmv.begin()));

	searcher->update(current, States(add, rmv));

	foreach (it, rmv.begin(), rmv.end())
		states.erase(*it);

	foreach (it, add.begin(), add.end())
		states.insert(*it);
}

void EpochSearcher::update(ExecutionState *current, States s)
{
	global_pool->update(current, s);

	epoch_state_c = 0;
	for (auto &e : epochs) {
		e.update(current, s);
		epoch_state_c += e.getNumStates();
	}

	pool_period = (epoch_state_c/2)+1;
}

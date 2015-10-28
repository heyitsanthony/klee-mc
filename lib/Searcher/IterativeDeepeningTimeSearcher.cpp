#include <iostream>
#include "klee/ExecutionState.h"
#include "klee/Internal/System/Time.h"
#include "static/Sugar.h"

#include "IterativeDeepeningTimeSearcher.h"

using namespace klee;

IterativeDeepeningTimeSearcher::IterativeDeepeningTimeSearcher(Searcher *_baseSearcher)
: baseSearcher(_baseSearcher)
, time(1.)
{}

IterativeDeepeningTimeSearcher::~IterativeDeepeningTimeSearcher()
{
	delete baseSearcher;
}

ExecutionState *IterativeDeepeningTimeSearcher::selectState(bool allowCompact)
{
	auto es = baseSearcher->selectState(allowCompact);

	startTime = util::estWallTime();
	if (es) return es;

	time *= 2;
	std::cerr << "KLEE: increasing time budget to: " << time << "\n";
	baseSearcher->update(
		NULL, States(pausedStates, States::emptySet));
	pausedStates.clear();

	return baseSearcher->selectState(allowCompact);
}

void IterativeDeepeningTimeSearcher::update(
	ExecutionState *current, const States s)
{
	double elapsed = util::estWallTime() - startTime;

	if (!s.getRemoved().empty()) {
		std::set<ExecutionState *> alt = s.getRemoved();
		foreach(it, s.getRemoved().begin(), s.getRemoved().end()) {
			ExecutionState *es = *it;
			ExeStateSet::const_iterator p_it = pausedStates.find(es);
			if (p_it != pausedStates.end()) {
				pausedStates.erase(p_it);
				alt.erase(alt.find(es));
			}
		}
		baseSearcher->update(current, States(s.getAdded(), alt));
	} else {
		baseSearcher->update(current, s);
	}

	if (current && !s.getRemoved().count(current) && elapsed > time) {
		pausedStates.insert(current);
		baseSearcher->removeState(current);
	}
}

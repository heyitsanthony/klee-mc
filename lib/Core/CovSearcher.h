#ifndef COVSEARCHER_H
#define COVSEARCHER_H

#include "klee/ExecutionState.h"
#include "StatsTracker.h"
#include "PrioritySearcher.h"

namespace klee
{
class CovPrioritizer : public Prioritizer
{
public:
	virtual ~CovPrioritizer() {}
	/* pr_k > pr_j => pr_k scheduled first */
	virtual int getPriority(ExecutionState& st)
	{ return (statTracker.isInstCovered(st.pc)) ? 1 : 0; }
	CovPrioritizer(StatsTracker& st)
	: statTracker(st) {}
private:
	StatsTracker&	statTracker;
};
}

#endif
#include <cassert>
#include "klee/Statistics.h"
#include "klee/Internal/ADT/RNG.h"
#include "klee/Internal/ADT/DiscretePDF.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/InstructionInfoTable.h"

#include "CoreStats.h"
#include "StatsTracker.h"
#include "Executor.h"
#include "static/Sugar.h"

#include "WeightedRandomSearcher.h"

using namespace klee;
namespace klee { extern RNG theRNG; }

WeightedRandomSearcher::WeightedRandomSearcher(Executor &_executor,
        WeightType _type)
: executor(_executor)
, states(new DiscretePDF<ExecutionState*>())
, type(_type)
{
	switch (type) {
	case Depth:
		updateWeights = false;
		break;
	case InstCount:
	case CPInstCount:
	case QueryCost:
	case MinDistToUncovered:
	case CoveringNew:
		updateWeights = true;
		break;
	default:
		assert(0 && "invalid weight type");
	}
}

WeightedRandomSearcher::~WeightedRandomSearcher() { delete states; }

ExecutionState &WeightedRandomSearcher::selectState(bool allowCompact)
{
	ExecutionState *es = states->choose(theRNG.getDoubleL(), allowCompact);
	return *es;
}

double WeightedRandomSearcher::getWeight(ExecutionState *es)
{
	if (es->isCompactForm || es->isReplayDone() == false)
		return es->weight;

	switch (type) {
	default:
	case Depth:
		break;
	case InstCount:
	{
		uint64_t count;
		count = theStatisticManager->getIndexedValue(
			stats::instructions,
			es->pc->info->id);
		double inv = 1. / std::max((uint64_t) 1, count);
		es->weight = inv * inv;
		break;
	}

	case CPInstCount:
	{
		StackFrame &sf = es->stack.back();
		uint64_t count;
		
		count = sf.callPathNode->statistics.getValue(
			stats::instructions);
		double inv = 1. / std::max((uint64_t) 1, count);
		es->weight = inv;
		break;
	}

	case QueryCost:
		es->weight = (es->queryCost < .1) ? 1. : 1. / es->queryCost;
		break;

	case CoveringNew:
	case MinDistToUncovered:
	{
		uint64_t md2u;
		
		md2u = computeMinDistToUncovered(
			es->pc,
			es->stack.back().minDistToUncoveredOnReturn);

		double invMD2U = 1. / (md2u ? md2u : 10000);

		if (type == MinDistToUncovered) {
			es->weight = invMD2U * invMD2U;
			break;
		}

		assert (type == CoveringNew);

		double invCovNew;
		invCovNew = (es->instsSinceCovNew)
			? 1. / std::max(1, (int) es->instsSinceCovNew - 1000)
			: 0.;
		es->weight = (invCovNew * invCovNew + invMD2U * invMD2U);
		break;
	}
	}

	return es->weight;
}

void WeightedRandomSearcher::update(ExecutionState *current, const States s)
{
	if (	current && 
		updateWeights && 
		!s.getRemoved().count(current) &&
		!s.getIgnored().count(current))
	{
		states->update(current, getWeight(current));
	}

	foreach (it, s.getAdded().begin(), s.getAdded().end()) {
		ExecutionState *es = *it;
		states->insert(es, getWeight(es), es->isCompactForm);
	}

	foreach (it, s.getIgnored().begin(), s.getIgnored().end())
		states->remove(*it);

	foreach (it, s.getUnignored().begin(), s.getUnignored().end()) {
		ExecutionState *es = *it;
		states->insert(es, getWeight(es), es->isCompactForm);
	}

	foreach (it, s.getRemoved().begin(), s.getRemoved().end())
		states->remove(*it);
}

bool WeightedRandomSearcher::empty() const { return states->empty(); }

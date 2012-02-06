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
WeightFunc::~WeightFunc(void) {}


WeightedRandomSearcher::WeightedRandomSearcher(
	Executor &_executor, WeightFunc* wf)
: executor(_executor)
, states(new DiscretePDF<ExecutionState*>())
, weigh_func(wf)
{}

WeightedRandomSearcher::~WeightedRandomSearcher()
{
	delete states;
	delete weigh_func;
}

ExecutionState &WeightedRandomSearcher::selectState(bool allowCompact)
{
	ExecutionState	*es;

	es = states->choose(theRNG.getDoubleL(), allowCompact);
	states->update(es, getWeight(es));
	es = states->choose(theRNG.getDoubleL(), allowCompact);

	double w = weigh_func->weigh(es);
	states->update(es, getWeight(es));

	std::cerr << "Selected Weight: " << es->weight << " / " <<  w << '\n';
	return *es;
}

double WeightedRandomSearcher::getWeight(ExecutionState *es)
{
	double	w;

	if (es->isCompact() || es->isReplayDone() == false)
		return es->weight;

	if (weigh_func->isUpdating()) {
		w = weigh_func->weigh(es);
		es->weight = w;
	}

	return es->weight;
}

void WeightedRandomSearcher::update(ExecutionState *current, const States s)
{
	static int	reweigh_c = 0;

	reweigh_c++;
	if (reweigh_c == 50) {
		for (unsigned i = 0; i < 50; i++) {
			ExecutionState	*es;

			es = states->choose(theRNG.getDoubleL(), false);
			states->update(es, getWeight(es));
		}

		reweigh_c = 0;
	}

	if (	current &&
		weigh_func->isUpdating() &&
		!s.getRemoved().count(current))
	{
		double	w = getWeight(current);
		std::cerr << "Updating Current Weight: " << w << '\n';
		states->update(current, w);
	}

	foreach (it, s.getAdded().begin(), s.getAdded().end()) {
		ExecutionState *es = *it;
		double		w = getWeight(es);
		std::cerr << "Adding Weight: " << w << '\n';
		states->insert(es, w, es->isCompact());
		executor.printStackTrace(*es, std::cerr);
	}

	foreach (it, s.getRemoved().begin(), s.getRemoved().end())
		states->remove(*it);
}

bool WeightedRandomSearcher::empty() const { return states->empty(); }

double InstCountWeight::weigh(const ExecutionState* es) const
{
	uint64_t count;
	count = theStatisticManager->getIndexedValue(
		stats::instructions,
		es->pc->getInfo()->id);
	double inv = 1. / std::max((uint64_t) 1, count);
	return inv * inv;
}

double CPInstCountWeight::weigh(const ExecutionState *es) const
{
	const StackFrame	 &sf(es->stack.back());
	uint64_t		count;

	count = sf.callPathNode->statistics.getValue(
		stats::instructions);
	double inv = 1. / std::max((uint64_t) 1, count);
	return inv;
}

double QueryCostWeight::weigh(const ExecutionState *es) const
{ return (es->queryCost < .1) ? 1. : 1. / es->queryCost; }

double CoveringNewWeight::weigh(const ExecutionState *es) const
{
	uint64_t	md2u;
	double		invCovNew, invMD2U;

	md2u = computeMinDistToUncovered(
		es->pc,
		es->stack.back().minDistToUncoveredOnReturn);

	invMD2U = 1. / (md2u ? md2u : 10000);
	invCovNew = (es->instsSinceCovNew)
		? 1. / std::max(1, (int) es->instsSinceCovNew - 1000)
		: 0.;

	return (invCovNew * invCovNew + invMD2U * invMD2U);
}

double DepthWeight::weigh(const ExecutionState* es) const
{ return es->weight; }

double MinDistToUncoveredWeight::weigh(const ExecutionState *es) const
{
	uint64_t md2u;
	double	invMD2U;

	md2u = computeMinDistToUncovered(
		es->pc,
		es->stack.back().minDistToUncoveredOnReturn);

	invMD2U = 1. / (md2u ? md2u : 10000);
	return invMD2U * invMD2U;
}

#include "static/Markov.h"

extern Markov<KFunction> theStackXfer;
double MarkovPathWeight::weigh(const ExecutionState* es) const
{
	KFunction	*last_kf = NULL;
	double		prob;

	prob = 1.0;
	foreach (it, es->stack.begin(), es->stack.end()) {
		KFunction	*kf;
		double		p;

		kf = (*it).kf;
		if (kf == NULL || last_kf == NULL) {
			last_kf = kf;
			continue;
		}

		p = theStackXfer.getProb(last_kf, kf);
		last_kf = kf;

		if (p == 0.0)
			continue;
		prob *= p;
	}

	return (1.0 - prob);
}

double ConstraintWeight::weigh(const ExecutionState* es) const
{ return es->constraints.size(); }

double TailWeight::weigh(const ExecutionState* es) const
{
	double		ret = 0;

	foreach (it, es->stack.begin(), es->stack.end()) {
		KFunction	*kf;

		kf = (*it).kf;
		if (kf == NULL)
			continue;

		ret += theStackXfer.getCount(kf);
	}

	return -ret;
}

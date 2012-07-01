#include <cassert>
#include "klee/Statistics.h"
#include "klee/Internal/ADT/RNG.h"
#include "klee/Internal/ADT/DiscretePDF.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/InstructionInfoTable.h"

#include "CoreStats.h"
#include "StatsTracker.h"
#include "Executor.h"
#include "Forks.h"
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

double PerInstCountWeight::weigh(const ExecutionState* es) const
{
	uint64_t count;
	count = theStatisticManager->getIndexedValue(
		stats::instructions,
		es->pc->getInfo()->id);
	double inv = 1. / std::max((uint64_t) 1, count);
	return inv * inv;
}


double StateInstCountWeight::weigh(const ExecutionState* es) const
{ return es->totalInsts; }

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

double CondSuccWeight::weigh(const ExecutionState* es) const
{
	if (es->prevForkCond.isNull())
		return 2;

	if (!exe->getForking()->hasSuccessor(es->prevForkCond))
		return 1;

	return 0;
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

double FreshBranchWeight::weigh(const ExecutionState* es) const
{ return es->isOnFreshBranch() ? 1.0 : 0.0; }


double TroughWeight::weigh(const ExecutionState* es) const
{
	if (stats::instructions > (last_ins+trough_width)) {
		/* many new instructions accumulated */
		/* recompute histogram */

		trough_hits.clear();
		foreach (it, exe->beginStates(), exe->endStates()) {
			const ExecutionState	*cur_st = *it;
			unsigned		idx;

			idx = cur_st->totalInsts/trough_width;
			trough_hits[idx] = trough_hits[idx] + 1;
		}

		last_ins = stats::instructions;
	}

	return trough_hits[es->totalInsts/trough_width];
}

/* returns distance from frontier of bucket */
double FrontierTroughWeight::weigh(const ExecutionState* es) const
{
	unsigned	es_idx;

	if (stats::instructions > (last_ins+trough_width/2)) {
		/* many new instructions accumulated */
		/* recompute histogram */
		foreach (it, trough_hits.begin(), trough_hits.end())
			delete it->second;
		trough_hits.clear();

		foreach (it, exe->beginStates(), exe->endStates()) {
			const ExecutionState	*cur_st = *it;
			std::set<unsigned>	*ss;
			unsigned		idx;

			idx = cur_st->totalInsts/trough_width;
			ss = trough_hits[idx];
			if (ss == NULL) {
				ss = new std::set<unsigned>();
				trough_hits[idx] = ss;
			}
			ss->insert(cur_st->totalInsts);
		}

		last_ins = stats::instructions;
	}


	es_idx = es->totalInsts/trough_width;

	std::set<unsigned>	*ss(trough_hits[es_idx]);
	int			gt = 0;

	if (ss == NULL)
		return 0;

	foreach (it, ss->begin(), ss->end()) {
		if (*it > es->totalInsts)
			gt++;
	}

	/* rank frontier as 0 (e.g. gt=0), rank last as ss.size() (e.g. gt) */
	return gt;
}

double BranchWeight::weigh(const ExecutionState* es) const
{
	unsigned	br_c, st_c;
	unsigned	br_prev_c, br_next_c;
	double		bias;

	loadIns();

	br_c = getBrCount(es->totalInsts);
	st_c = getStCount(es->totalInsts);
	br_prev_c = getBrCount(
		(es->totalInsts > n_width)
		? es->totalInsts - n_width
		: 0);
	br_next_c = getBrCount(es->totalInsts + n_width);

	if (st_c == 0) return 0;
	if (br_prev_c == 0) br_prev_c = 1;
	if (br_next_c == 0) br_next_c = 1;

	bias = 1.0 - (double)(es->totalInsts % n_width) / (double)n_width;
//	return  bias*((double)br_c) / ((double)st_c*br_prev_c*br_next_c);
//	return  ((double)br_c) / ((double)st_c*br_prev_c*br_next_c);
	return  ((double)br_c) / ((double)st_c);
}


unsigned BranchWeight::getStCount(uint64_t es_ins) const
{
	st_ins_ty::const_iterator	it, it_end;
	uint64_t			ins_upper, ins_lower;
	unsigned			st_c;

	ins_upper = es_ins + n_width;
	ins_lower = (es_ins > n_width) ? (es_ins - n_width) : 0;

	it_end = st_ins.end();
	it = st_ins.lower_bound(ins_lower);
	st_c = 0;
	while (it != it_end) {
		unsigned		ins(it->first);

		it++;
		if (ins > ins_upper)
			break;

		st_c++;
	}

	return st_c;
}

unsigned BranchWeight::getBrCount(uint64_t es_ins) const
{
	br_ins_ty::const_iterator	it, it_end;
	uint64_t			ins_upper, ins_lower;
	unsigned			br_c;

	ins_upper = es_ins + n_width;
	// ins_lower = (es_ins > n_width) ? (es_ins - n_width) : 0;
	ins_lower = es_ins;
	/* only consider branches directly ahead of us */

	it_end = br_ins.end();
	it = br_ins.lower_bound(ins_lower);
	br_c = 0;
	while (it != it_end) {
		unsigned		ins(it->first);

		it++;
		if (ins > ins_upper)
			break;

		br_c++;
	}

	return br_c;
}

#define BR_COMPUTE_INTERVAL	10	/* min 10 instructions must pass */
void BranchWeight::loadIns(void) const
{
	if (stats::instructions < (last_ins+BR_COMPUTE_INTERVAL)) 
		return;

	last_ins = stats::instructions;
	br_ins.clear();
	foreach (it, KBrInstruction::beginBr(), KBrInstruction::endBr()) {
		const KBrInstruction*	kbr(*it);
		if (kbr->hasFoundAll() || !kbr->hasSeenExpr())
			continue;
		if (kbr->getTrueMinInst() != ~((uint64_t)0)) {
			br_ins.insert(std::make_pair(kbr->getTrueMinInst(), kbr));
			continue;
		}
		if (kbr->getFalseMinInst() != ~((uint64_t)0)) {
			br_ins.insert(std::make_pair(kbr->getFalseMinInst(), kbr));
			continue;
		}
		/* Hm. */
	}

	st_ins.clear();
	foreach (it, exe->beginStates(), exe->endStates()) {
		const ExecutionState	*es(*it);
		st_ins.insert(std::make_pair(es->totalInsts, es));
	}
}

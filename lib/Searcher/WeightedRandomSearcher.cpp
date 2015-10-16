#include <cassert>
#include "klee/Statistics.h"
#include "klee/Internal/ADT/RNG.h"
#include "klee/Internal/ADT/DiscretePDF.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KFunction.h"
#include "klee/Internal/Module/InstructionInfoTable.h"

#include "../Core/CoreStats.h"
#include "../Core/StatsTracker.h"
#include "../Core/Executor.h"
#include "../Core/Forks.h"
#include "static/Sugar.h"

#include "WeightedRandomSearcher.h"

using namespace klee;
namespace klee { extern RNG theRNG; }
WeightFunc::~WeightFunc(void) {}


WeightedRandomSearcher::WeightedRandomSearcher(
	Executor &_executor, WeightFunc* wf)
: executor(_executor)
, pdf(new DiscretePDF<ExecutionState*>())
, weigh_func(wf)
{}

WeightedRandomSearcher::~WeightedRandomSearcher()
{
	delete pdf;
	delete weigh_func;
}

ExecutionState &WeightedRandomSearcher::selectState(bool allowCompact)
{
	ExecutionState	*es;

	es = pdf->choose(theRNG.getDoubleL(), allowCompact);
	pdf->update(es, getWeight(es));
	es = pdf->choose(theRNG.getDoubleL(), allowCompact);

	// double w = weigh_func->weigh(es);
	pdf->update(es, getWeight(es));
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

#if 0
	if (	current &&
		weigh_func->isUpdating() &&
		!s.getRemoved().count(current))
	{
		double	w = getWeight(current);
		pdf->update(current, getWeight(current));
	}
#endif
	foreach (it, s.getAdded().begin(), s.getAdded().end()) {
		ExecutionState *es = *it;
		double		w = getWeight(es);
		pdf->insert(es, w, es->isCompact());
	}

	foreach (it, s.getRemoved().begin(), s.getRemoved().end())
		pdf->remove(*it);

	reweigh_c++;
	if (reweigh_c == 16 && current) {
		for (unsigned i = 0; i < 100; i++) {
			auto es = pdf->choose(theRNG.getDoubleL(), false);
			pdf->update(es, getWeight(es));
		}

		reweigh_c = 0;
	}
}

bool WeightedRandomSearcher::empty() const { return pdf->empty(); }

#include "klee/Internal/Module/KInstruction.h"
#include "BranchPredictors.h"
#include "klee/Internal/ADT/RNG.h"
#include "static/Sugar.h"

namespace klee { extern RNG theRNG; }

using namespace klee;


RandomPredictor::RandomPredictor()
: phase_hint(0)
, period(32)
, period_bump(2) {}

bool RandomPredictor::predict(
	const ExecutionState& st,
	KInstruction* ki,
	bool& hint)
{
	KBrInstruction	*kbr;
	bool		fresh_false, fresh_true;

	kbr = static_cast<KBrInstruction*>(ki);
	fresh_false = (kbr->hasFoundFalse() == false);
	fresh_true = (kbr->hasFoundTrue() == false);

	/* no freshbranch hint to give */
	if (!fresh_false && !fresh_true) {
		unsigned	hit_t, hit_f;

		hit_t = kbr->getTrueHits();
		hit_f = kbr->getFalseHits();

		if (hit_t > 1) {
			if (hit_t == hit_f)
				return false;
		}

		/* retry */
		if (hit_t == 1)
			hint = true;
		else if (hit_f == 1)
			hint = false;
		else
			hint = theRNG.getBool();
		return true;
	}

	if (fresh_false && fresh_true) {
		/* two-way branch-- flip coin to avoid bias */
		hint = theRNG.getBool();
	} else if (fresh_false)
		hint = false;
	else /* if (fresh_true) */
		hint = true;

	return true;
}

// some notes for a periodic branch predictor:
#if 0		
	{
		hint = (((phase_hint++) % period) < (period/2))
			? 1 : 0;
		if ((phase_hint % period) == 0) {
			phase_hint = 0;
			period_bump = 8-(rand() % 16);
			period += period_bump;
			if (period <= 0) {
				period_bump = 1;
				period = 32;
			} else if (period > 64) {
				period_bump = -1;
				period = 32;
			}
		}
	}
#endif
// BAD RULE: DOESN'T WORK ON FORKS.
// hint = hit_t < hit_f;
// Hit count is recorded before state is dispatched,
// so if a branch always forks, it'll always have
// hit_t < hit_f-- which will bias things in favor of false.
// OK:
// hint = (kbr->getForkHits()/8) % 2;

bool SeqPredictor::predict(
	const ExecutionState& st,
	KInstruction* ki,
	bool& hint)
{
	hint = seq[idx++ % seq.size()];
	return true;
}

RotatingPredictor::RotatingPredictor()
: period(100)
, tick(0)
{}

RotatingPredictor::~RotatingPredictor()
{
	foreach (it, bps.begin(), bps.end())
		delete (*it);
}

bool RotatingPredictor::predict(
	const ExecutionState& st,
	KInstruction* ki,
	bool& hint)
{
	return bps[(tick++ / period) % bps.size()]->predict(st, ki, hint);
}


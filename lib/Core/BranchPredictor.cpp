#include "klee/Internal/Module/KInstruction.h"
#include "BranchPredictor.h"
#include "klee/Internal/ADT/RNG.h"

namespace klee { extern RNG theRNG; }

using namespace klee;


BranchPredictor::BranchPredictor()
: phase_hint(0)
, period(32)
, period_bump(2) {}

bool BranchPredictor::predict(
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
		else {
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

		// BAD RULE: DOESN'T WORK ON FORKS.
		// hint = hit_t < hit_f;
		// Hit count is recorded before state is dispatched,
		// so if a branch always forks, it'll always have
		// hit_t < hit_f-- which will bias things in favor of false.

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

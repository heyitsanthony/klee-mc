#ifndef COVSEARCHER_H
#define COVSEARCHER_H

#include "klee/ExecutionState.h"
#include "../Core/StatsTracker.h"
#include "PrioritySearcher.h"

#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"

namespace klee
{
class CovPrioritizer : public Prioritizer
{
public:
	virtual ~CovPrioritizer() {}
	/* pr_k > pr_j => pr_k scheduled first */
	virtual int getPriority(ExecutionState& st)
	{
		KInstruction	*ki = st.pc;
		llvm::Function	*f;
		KFunction	*kf;

		/* super-priority */
		if (statTracker.isInstCovered(st.pc) == false)
			return 2;

		f = ki->getInst()->getParent()->getParent();
		kf = km->getKFunction(f);

		/* don't even know the kfunc? we're in trouble */
		if (kf == NULL)
			return 0;

		if (isFuncCovered(kf))
			return 0;

		return 1;
	}

	CovPrioritizer(const KModule* _km, StatsTracker& st)
	: statTracker(st)
	, km(_km) {}

	virtual Prioritizer* copy(void) const
	{ return new CovPrioritizer(km, statTracker); }

protected:
	bool isFuncCovered(const KFunction* kf)
	{
		for (unsigned i = 0; i < kf->numInstructions; i++) {
			if (!statTracker.isInstCovered(kf->instructions[i]))
				return false;
		}
		return true;
	}


private:
	StatsTracker&	statTracker;
	const KModule*	km;
};
}

#endif
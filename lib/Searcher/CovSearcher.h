#ifndef COVSEARCHER_H
#define COVSEARCHER_H

#include "klee/ExecutionState.h"
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
		KInstruction		*ki = st.pc;
		const llvm::Function	*f;
		KFunction		*kf;

		/* super-priority */
		if (st.pc->isCovered() == false)
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

	CovPrioritizer(const KModule* _km) : km(_km) {}

	virtual Prioritizer* copy(void) const
	{ return new CovPrioritizer(km); }

protected:
	bool isFuncCovered(const KFunction* kf) const {
		for (unsigned i = 0; i < kf->numInstructions; i++) {
			if (!kf->instructions[i]->isCovered())
				return false;
		}
		return true;
	}

private:
	const KModule*	km;
};
}

#endif
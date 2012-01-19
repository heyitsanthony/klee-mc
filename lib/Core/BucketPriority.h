#ifndef BUCKETPRIORITY_H
#define BUCKETPRIORITY_H

#include "CoreStats.h"
#include "StatsTracker.h"
#include "PrioritySearcher.h"

namespace klee
{

class BucketPrioritizer : public Prioritizer
{
public:
	BucketPrioritizer() {}
	virtual ~BucketPrioritizer() {}

	/* pr_k > pr_j => pr_k scheduled first */
	virtual int getPriority(ExecutionState& st)
	{
		llvm::Function	*f;
		uint64_t	hits;

		f = getHitFunction(st);
		hits = hitmap[f];
		if (st.coveredNew && hits < 4)
			return -1;

		if (!isLatched()) {
			std::cerr << "Penalty: " << f->getNameStr()
				<< ". hits=" << hits << "\n";
			hitmap[f] = hits+1;
		}

		return -getStackRank(st);
	}
private:
	llvm::Function* getHitFunction(ExecutionState& st) const
	{
		// necessary so that we don't absorb helper functions
		// from vexllvm-- improper bucketing!
		if (st.stack.size() && st.stack.back().kf != NULL)
			return st.stack.back().kf->function;
		else
			return st.pc->getInst()->getParent()->getParent();
	}

	int getStackRank(ExecutionState& st) const
	{
		ExecutionState::stack_ty& s(st.stack);
		int	rank = 1;

		if (s.size() == 0)
			return hitmap.find(getHitFunction(st))->second;

		for (unsigned i = 0; i < s.size(); i++) {
			KFunction			*kf;
			hitmap_ty::const_iterator	it;
			int				hits;

			kf = s[i].kf;
			if (kf == NULL)
				continue;

			it = hitmap.find(kf->function);
			if (it == hitmap.end())
				continue;

			hits = it->second;
			if (!hits)
				continue;

			rank *= 1+((it->second)/(i+1));
		}

		return rank;
	}

protected:
	typedef std::map<llvm::Function*, uint64_t> hitmap_ty;
	hitmap_ty	hitmap;
};
}
#endif
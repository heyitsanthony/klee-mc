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
	/* TODO: use stacklist */
		llvm::Function	*f;
		uint64_t	hits;

		if (st.coveredNew)
			return -1;

		// f = st.pc->getInst()->getParent()->getParent();

		// necessary so that we don't absorb helper functions
		// from vexllvm-- improper bucketing!
		f = st.stack.front().kf->function;
		hits = hitmap[f];

		if (!isLatched()) {
			std::cerr << "Penalty: " << f->getNameStr()
				<< ". hits=" << hits << "\n";
			hitmap[f] = hits+1;
		}

		return -(hits/2);
	}
private:
protected:
	typedef std::map<llvm::Function*, uint64_t> hitmap_ty;
	hitmap_ty	hitmap;
};
}
#endif

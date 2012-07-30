#ifndef HISTOPRIORITY_H
#define HISTOPRIORITY_H

#include "../Core/Executor.h"
#include "../Core/CoreStats.h"
#include "../Core/StatsTracker.h"
#include "PrioritySearcher.h"

#include "static/Sugar.h"

namespace klee
{

class HistoPrioritizer : public Prioritizer
{
public:
	HistoPrioritizer(Executor& _exe) : exe(_exe), last_ins(~0) {}
	virtual ~HistoPrioritizer() {}

	virtual Prioritizer* copy(void) const
	{ return new HistoPrioritizer(exe); }

	/* pr_k > pr_j => pr_k scheduled first */
	virtual int getPriority(ExecutionState& st)
	{

		const KFunction		*kf;

		kf = st.stack.empty() ? NULL : st.stack.back().kf;
		if (stats::instructions == last_ins)
			return -histo[kf];

		last_ins = stats::instructions;
		histo.clear();
		foreach (it, exe.beginStates(), exe.endStates()) {
			const ExecutionState	*cur_st = *it;
			const KFunction		*cur_kf;

			if (cur_st->stack.empty())
				continue;

			cur_kf = cur_st->stack.back().kf;
			histo[cur_kf] = histo[cur_kf] + 1;
		}

		return -histo[kf];
	}

	virtual void printName(std::ostream &os) const { os << "Histo"; }
private:
	typedef std::map<const KFunction*, uint64_t>	histo_ty;
	histo_ty	histo;
	Executor	&exe;
	uint64_t	last_ins;
};
}
#endif
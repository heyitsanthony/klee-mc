#ifndef TAILPRIORITY_H
#define TAILPRIORITY_H

#include "CoreStats.h"
#include "StatsTracker.h"
#include "PrioritySearcher.h"

#include "static/Sugar.h"

namespace klee
{

class TailPrioritizer : public Prioritizer
{
public:
	TailPrioritizer() {}
	virtual ~TailPrioritizer() {}

	DEFAULT_PR_COPY(TailPrioritizer)

	/* pr_k > pr_j => pr_k scheduled first */
	virtual int getPriority(ExecutionState& st)
	{	
		trace_ty		trace;
		tracemap_ty::iterator	it;
		int			hits;

		if (!st.stack.empty()) {
			for (unsigned i = 0; i < st.stack.size()-1; i++) {
				KFunction	*kf;

				kf = st.stack[i].kf;
				if (kf == NULL)
					continue;
				trace.push_back(kf);
			}
		}

		if (trace.size() > 1)
			trace.pop_back();

		it = trace2count.find(trace);
		if (it == trace2count.end()) {
			trace2count[trace] = 1;
			hits = 1;
		} else {
			hits = (*it).second+1;
			(*it).second = hits;
		}

		return -hits;
	}
private:
	typedef std::vector<KFunction*>	trace_ty;
	typedef std::map<trace_ty, uint64_t>	tracemap_ty;
	tracemap_ty	trace2count;
};
}
#endif
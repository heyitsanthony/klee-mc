#ifndef RRPrSEARCHER_H
#define RRPrSEARCHER_H

#include "../Core/Searcher.h"
#include "PrioritySearcher.h"

namespace klee
{
class RRPrSearcher : public Searcher
{
public:
	RRPrSearcher(Prioritizer* pr_)
		: pr(pr_)
		, last_pr(0)
	{}
	virtual ~RRPrSearcher() {}

	Searcher* createEmpty(void) const override
	{ return new RRPrSearcher(pr->copy()); }

	ExecutionState *selectState(bool allowCompact) override;
	void update(ExecutionState *current, const States s) override;
	bool empty() const override { return states.empty(); }
	void printName(std::ostream &os) const override {
		os << "RRPrSearcher(";
		pr->printName(os);
		os << ")";
	}

private:
	Prioritizer	*pr;
	int		last_pr;
	std::set<ExecutionState*> states;
};
}


#endif

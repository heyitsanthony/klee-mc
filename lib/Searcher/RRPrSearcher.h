#ifndef RRPrSEARCHER_H
#define RRPrSEARCHER_H

#include "../Core/Searcher.h"
#include "PrioritySearcher.h"

namespace klee
{
class RRPrSearcher : public Searcher
{
public:
	RRPrSearcher(Prioritizer* pr_, int cut_off_ = INT_MIN)
		: pr(pr_)
		, last_pr(0)
		, cut_off(cut_off_)
	{}
	virtual ~RRPrSearcher() {}

	Searcher* createEmpty(void) const override {
		return new RRPrSearcher(pr->copy(), cut_off);
	}

	ExecutionState *selectState(bool allowCompact) override;
	void update(ExecutionState *current, const States s) override;
	void printName(std::ostream &os) const override {
		os << "RRPrSearcher(";
		pr->printName(os);
		os << ")";
	}

private:
	Prioritizer	*pr;
	int		last_pr;
	int		cut_off;
	std::set<ExecutionState*> states;
};
}


#endif

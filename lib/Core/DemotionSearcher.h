#ifndef DEMOTIONSEARCHER_H
#define DEMOTIONSEARCHER_H

#include "klee/ExecutionState.h"
#include "Searcher.h"

namespace klee {
class DemotionSearcher : public Searcher
{
public:
	ExecutionState &selectState(bool allowCompact);
	DemotionSearcher(Searcher* _searcher_base, unsigned max_repeats = 10);
	virtual ~DemotionSearcher(void) { delete searcher_base; }

	virtual Searcher* createEmpty(void) const
	{ return new DemotionSearcher(searcher_base->createEmpty()); }

	void update(ExecutionState *current, States s);

	bool empty(void) const
	{ return (scheduled.size() + demoted.size()) == 0; }

	void printName(std::ostream &os) const
	{
		os << "<DemotionSearcher>\n";
		searcher_base->printName(os);
		os << "</DemotionSearcher>\n";
	}

private:
	void recoverDemoted(void);

	ExeStateSet	scheduled;
	ExeStateSet	demoted;
	Searcher	*searcher_base;
	ExecutionState	*last_current;
	unsigned	repeat_c;
	unsigned	max_repeats;
	uint64_t	last_ins_uncov;
};
}

#endif

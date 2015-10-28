#ifndef DEMOTIONSEARCHER_H
#define DEMOTIONSEARCHER_H

#include "klee/ExecutionState.h"
#include "../Core/Searcher.h"

namespace klee {
class DemotionSearcher : public Searcher
{
public:
	DemotionSearcher(Searcher* _searcher_base, unsigned max_repeats = 10);

	Searcher* createEmpty(void) const override
	{ return new DemotionSearcher(searcher_base->createEmpty()); }

	ExecutionState *selectState(bool allowCompact) override;
	void update(ExecutionState *current, States s) override;

	void printName(std::ostream &os) const override {
		os << "<DemotionSearcher>\n";
		searcher_base->printName(os);
		os << "</DemotionSearcher>\n";
	}

private:
	void recoverDemoted(void);

	ExeStateSet	scheduled;
	ExeStateSet	demoted;
	usearcher_t 	searcher_base;
	ExecutionState	*last_current;
	unsigned	repeat_c;
	unsigned	max_repeats;
	uint64_t	last_ins_uncov;
};
}

#endif

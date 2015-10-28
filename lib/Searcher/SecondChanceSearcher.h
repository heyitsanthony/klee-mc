#ifndef SECONDCHANCESEARCHER_H
#define SECONDCHANCESEARCHER_H

#include "klee/ExecutionState.h"
#include "../Core/Searcher.h"

namespace klee {
class SecondChanceSearcher : public Searcher
{
public:
	SecondChanceSearcher(Searcher* _searcher_base);
	virtual ~SecondChanceSearcher(void) { delete searcher_base; }

	Searcher* createEmpty(void) const override {
		return new SecondChanceSearcher(searcher_base->createEmpty());
	}

	ExecutionState* selectState(bool allowCompact)  override;
	void update(ExecutionState *current, States s) override;

	void printName(std::ostream &os) const override
	{
		os << "<SecondChanceSearcher>\n";
		searcher_base->printName(os);
		os << "</SecondChanceSearcher>\n";
	}

private:
	void updateInsCounts(void);

	Searcher	*searcher_base;
	ExecutionState	*last_current;
	unsigned	remaining_quanta;
	uint64_t	last_ins_total;
	uint64_t	last_ins_cov;
};
}

#endif

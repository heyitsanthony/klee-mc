#ifndef SECONDCHANCESEARCHER_H
#define SECONDCHANCESEARCHER_H

#include "klee/ExecutionState.h"
#include "Searcher.h"

namespace klee {
class SecondChanceSearcher : public Searcher
{
public:
	ExecutionState &selectState(bool allowCompact);
	SecondChanceSearcher(Searcher* _searcher_base);
	virtual ~SecondChanceSearcher(void) { delete searcher_base; }

	virtual Searcher* createEmpty(void) const
	{ return new SecondChanceSearcher(searcher_base->createEmpty()); }

	void update(ExecutionState *current, States s);

	bool empty(void) const { return searcher_base->empty(); }

	void printName(std::ostream &os) const
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

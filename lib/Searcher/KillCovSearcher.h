#ifndef KILLCOVSEARCHER_H
#define KILLCOVSEARCHER_H

#include "klee/ExecutionState.h"
#include "../Core/Searcher.h"

namespace klee {
class KillCovSearcher : public Searcher
{
public:
	ExecutionState *selectState(bool allowCompact);
	KillCovSearcher(Executor& exe, Searcher* _searcher_base);
	virtual ~KillCovSearcher(void) { delete searcher_base; }

	virtual Searcher* createEmpty(void) const
	{ return new KillCovSearcher(exe, searcher_base->createEmpty()); }

	void update(ExecutionState *current, States s);

	bool empty(void) const { return searcher_base->empty(); }

	void printName(std::ostream &os) const
	{
		os << "<KillCovSearcher>\n";
		searcher_base->printName(os);
		os << "</KillCovSearcher>\n";
	}

private:
	void updateInsCounts(void);
	void killForked(ExecutionState* exe);

	Executor	&exe;
	Searcher	*searcher_base;
	ExecutionState	*last_current;

	uint64_t	last_ins_total;
	uint64_t	last_ins_cov;

	ExecutionState	*forked_current;
	bool		found_instructions;
};
}

#endif

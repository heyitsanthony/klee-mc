#ifndef CONCSEARCHER_H
#define CONCSEARCHER_H

#include "Searcher.h"

namespace klee
{

class ConcretizingSearcher : public Searcher
{
public:
	ExecutionState &selectState(bool allowCompact);

	ConcretizingSearcher(
		Executor& _exe,
		Searcher* _searcher_base)
	: searcher_base(_searcher_base)
	, exe(_exe)
	, last_es(0)
	, last_cov(0)
	, concretized(false) {}

	virtual ~ConcretizingSearcher(void) { delete searcher_base; }

	virtual Searcher* createEmpty(void) const
	{ return new ConcretizingSearcher(exe, searcher_base->createEmpty()); }

	void update(ExecutionState *current, States s);

	bool empty(void) const { return searcher_base->empty(); }

	virtual void printName(std::ostream &os) const
	{
		os << "<ConcretizingSearcher>\n";
		searcher_base->printName(os);
		os << "</ConcretizingSearcher>\n";
	}

private:
	Searcher		*searcher_base;
	Executor		&exe;
	ExecutionState		*last_es;
	uint64_t		last_cov;
	bool			concretized;

};

}

#endif


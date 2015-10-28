#ifndef CONCSEARCHER_H
#define CONCSEARCHER_H

#include "../Core/Searcher.h"

namespace klee
{

class ConcretizingSearcher : public Searcher
{
public:
	ConcretizingSearcher(
		Executor& _exe,
		Searcher* _searcher_base)
	: searcher_base(_searcher_base)
	, exe(_exe)
	, last_es(0)
	, last_cov(0)
	, concretized(false) {}

	Searcher* createEmpty(void) const override
	{ return new ConcretizingSearcher(exe, searcher_base->createEmpty()); }

	ExecutionState* selectState(bool allowCompact) override;
	void update(ExecutionState *current, States s) override;
	void printName(std::ostream &os) const override
	{
		os << "<ConcretizingSearcher>\n";
		searcher_base->printName(os);
		os << "</ConcretizingSearcher>\n";
	}

private:
	usearcher_t	 searcher_base;
	Executor	&exe;
	ExecutionState	*last_es;
	uint64_t	last_cov;
	bool		concretized;

};

}

#endif


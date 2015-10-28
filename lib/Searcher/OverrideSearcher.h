#ifndef OVERRIDESEARCHER_H
#define OVERRIDESEARCHER_H

#include "../Core/Searcher.h"

namespace klee
{

class OverrideSearcher : public Searcher
{
public:
	ExecutionState *selectState(bool allowCompact);

	OverrideSearcher(Searcher* _searcher_base)
	: searcher_base(_searcher_base) {}

	virtual ~OverrideSearcher(void) { delete searcher_base; }

	virtual Searcher* createEmpty(void) const
	{ return new OverrideSearcher(searcher_base->createEmpty()); }

	void update(ExecutionState *current, States s);

	bool empty(void) const { return searcher_base->empty(); }

	virtual void printName(std::ostream &os) const
	{
		os << "<OverrideSearcher>\n";
		searcher_base->printName(os);
		os << "</OverrideSearcher>\n";
	}

	/* returns old override (or null if nothing around) */
	static ExecutionState* setOverride(ExecutionState* es)
	{
		ExecutionState	*old_es = override_es;
		override_es = es;
		return old_es;
	}

private:
	Searcher		*searcher_base;
	static ExecutionState	*override_es;
};

}

#endif


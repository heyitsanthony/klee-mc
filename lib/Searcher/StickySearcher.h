#ifndef StickySEARCHER_H
#define StickySEARCHER_H

#include "../Core/Searcher.h"

namespace klee
{
class StickySearcher : public Searcher
{
public:
	StickySearcher(Searcher* _base) : base(_base), sticky_st(NULL) {}
	ExecutionState *selectState(bool allowCompact);
	virtual Searcher* createEmpty(void) const
	{ return new StickySearcher(base->createEmpty()); }
	void update(ExecutionState *current, States s);
	bool empty() const { return base->empty(); }
	void printName(std::ostream &os) const { os << "StickySearcher\n"; }
private:
	Searcher	*base;
	ExecutionState	*sticky_st;
};
}


#endif

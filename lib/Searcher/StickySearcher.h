#ifndef StickySEARCHER_H
#define StickySEARCHER_H

#include "../Core/Searcher.h"

namespace klee
{
class StickySearcher : public Searcher
{
public:
	StickySearcher(Searcher* _base) : base(_base), sticky_st(NULL) {}

	Searcher* createEmpty(void) const override {
		return new StickySearcher(base->createEmpty());
	}
	ExecutionState *selectState(bool allowCompact) override;
	void update(ExecutionState *current, States s)  override;
	void printName(std::ostream &os) const override {
		os << "StickySearcher\n";
	}
private:
	Searcher	*base;
	ExecutionState	*sticky_st;
};
}


#endif

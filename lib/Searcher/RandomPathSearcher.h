#ifndef RANDOMPATHSEARCHER_H
#define RANDOMPATHSEARCHER_H

#include "../Core/Searcher.h"

namespace klee
{
class RandomPathSearcher : public Searcher 
{
	Executor &executor;

public:
	RandomPathSearcher(Executor &_executor);
	virtual ~RandomPathSearcher() {}

	virtual Searcher* createEmpty(void) const
	{ return new RandomPathSearcher(executor); }

	ExecutionState* selectState(bool allowCompact);
	void update(ExecutionState *current, const States s);
	bool empty() const;
	void printName(std::ostream &os) const { os << "RandomPathSearcher\n"; }
};
}
#endif

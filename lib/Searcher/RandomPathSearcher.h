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

	Searcher* createEmpty(void) const override {
		return new RandomPathSearcher(executor);
	}
	ExecutionState* selectState(bool allowCompact) override;
	void update(ExecutionState *current, const States s) override;
	void printName(std::ostream &os) const override {
		os << "RandomPathSearcher\n";
	}
};
}
#endif

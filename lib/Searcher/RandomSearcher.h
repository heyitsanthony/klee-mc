#ifndef RANDOMSEARCHER_H
#define RANDOMSEARCHER_H

#include "../Core/Searcher.h"

namespace klee
{
class RandomSearcher : public Searcher
{
	std::vector<ExecutionState*> states;
	std::vector<ExecutionState*> statesNonCompact;

public:
	Searcher* createEmpty(void) const override {
		return new RandomSearcher();
	}

	ExecutionState* selectState(bool allowCompact) override;
	void update(ExecutionState *current, const States s) override;
	void printName(std::ostream &os) const override {
		os << "RandomSearcher\n";
	}
};
}

#endif

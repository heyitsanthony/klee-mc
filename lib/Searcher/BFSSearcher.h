#ifndef BFSSEARCHER_H
#define BFSSEARCHER_H

#include <deque>
#include "../Core/Searcher.h"

namespace klee
{
class BFSSearcher : public Searcher
{
public:
	Searcher* createEmpty(void) const override {
		return new BFSSearcher();
	}
	ExecutionState* selectState(bool allowCompact) override;
	void update(ExecutionState *current, States s) override;
	void printName(std::ostream &os) const override {
		os << "BFSSearcher\n";
	}
private:
	std::deque<ExecutionState*> states;
};
}


#endif

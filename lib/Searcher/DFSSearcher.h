#ifndef DFSSEARCHER_H
#define DFSSEARCHER_H

#include <list>
#include "../Core/Searcher.h"

namespace klee
{
class DFSSearcher : public Searcher
{
public:
	Searcher* createEmpty(void) const override {
		return new DFSSearcher();
	}
	ExecutionState *selectState(bool allowCompact) override;
	void update(ExecutionState *current, States s) override;
	void printName(std::ostream &os) const override {
		os << "DFSSearcher\n";
	}
private:
	std::list<ExecutionState*> states;
};
}


#endif

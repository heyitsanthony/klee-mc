#ifndef RRSEARCHER_H
#define RRSEARCHER_H

#include "../Core/Searcher.h"

namespace klee
{
class RRSearcher : public Searcher
{
	std::list<ExecutionState*>		states;
	std::list<ExecutionState*>::iterator cur_state;
public:
	RRSearcher() : cur_state(states.end()) {}
	virtual ~RRSearcher() {}

	Searcher* createEmpty(void) const override {
		return new RRSearcher();
	}
	ExecutionState* selectState(bool allowCompact)  override;
	void update(ExecutionState *current, const States s) override;
	void printName(std::ostream &os) const override { os << "RRSearcher\n"; }
};
}


#endif

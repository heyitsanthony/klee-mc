#ifndef RESCANSEARCHER_H
#define RESCANSEARCHER_H

#include <queue>
#include "PrioritySearcher.h"

namespace klee
{
class RescanSearcher : public Searcher
{
public:
	RescanSearcher(Prioritizer* _pr) : pr(_pr), state_c(0) {}
	virtual ~RescanSearcher(void) { delete pr; }

	Searcher* createEmpty(void) const override {
		return new RescanSearcher(pr->copy());
	}
	ExecutionState* selectState(bool allowCompact) override;
	void update(ExecutionState *current, States s) override;
	void printName(std::ostream &os) const override {
		os << "RescanSearcher("; pr->printName(os); os << ")";
	}
private:
	Prioritizer*			pr;
	typedef std::list<ExecutionState*> state_list_ty;
	state_list_ty	states;
	unsigned	state_c;
};
}
#endif

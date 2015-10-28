#ifndef RESCANSEARCHER_H
#define RESCANSEARCHER_H

#include <queue>
#include "PrioritySearcher.h"

namespace klee
{
class RescanSearcher : public Searcher
{
public:
	ExecutionState* selectState(bool allowCompact);
	RescanSearcher(Prioritizer* _pr) : pr(_pr), state_c(0) {}
	virtual ~RescanSearcher(void) { delete pr; }
	void update(ExecutionState *current, States s);

	virtual Searcher* createEmpty(void) const
	{ return new RescanSearcher(pr->copy()); }
	bool empty(void) const { return state_c == 0; }
	void printName(std::ostream &os) const
	{ os << "RescanSearcher("; pr->printName(os); os << ")"; }
private:
	Prioritizer*			pr;
	typedef std::list<ExecutionState*> state_list_ty;
	state_list_ty	states;
	unsigned	state_c;
};
}
#endif

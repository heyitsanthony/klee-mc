#ifndef PHASEDSEARCHER_H
#define PHASEDSEARCHER_H

#include "../Core/Searcher.h"

namespace klee
{
class PhasedSearcher : public Searcher
{
public:
	virtual Searcher* createEmpty(void) const
	{ return new PhasedSearcher(); }

	ExecutionState &selectState(bool allowCompact);
	PhasedSearcher() : state_c(0), cur_phase(0) {}
	virtual ~PhasedSearcher() {}

	void update(ExecutionState *current, States s);
	bool empty() const { return state_c == 0; }
	void printName(std::ostream &os) const { os << "PhasedSearcher\n"; }

private:
	void updateCurrent(ExecutionState* current);

	std::vector<std::list<ExecutionState*> >	phases;
	typedef std::map<ExecutionState*, unsigned> backmap_ty;
	backmap_ty				state_backmap;
	unsigned int				state_c;
	unsigned int				cur_phase;
};
}

#endif

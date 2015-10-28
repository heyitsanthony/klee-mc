#ifndef PHASEDSEARCHER_H
#define PHASEDSEARCHER_H

#include "../Core/Searcher.h"

namespace klee
{
class PhasedSearcher : public Searcher
{
public:
	PhasedSearcher() : state_c(0), cur_phase(0) {}

	Searcher* createEmpty(void) const override {
		return new PhasedSearcher();
	}
	ExecutionState *selectState(bool allowCompact) override;
	void update(ExecutionState *current, States s) override;
	void printName(std::ostream &os) const override {
		os << "PhasedSearcher\n";
	}

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

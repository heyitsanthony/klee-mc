#ifndef EPOCHSEARCHER_H
#define EPOCHSEARCHER_H

#include "Searcher.h"

namespace klee {
class EpochSearcher : public Searcher
{
public:
	ExecutionState &selectState(bool allowCompact);
	EpochSearcher(
		Executor& _exe,
		Searcher* _searcher_base,
		Searcher* global_pool);
	virtual ~EpochSearcher(void);

	virtual Searcher* createEmpty(void) const
	{ return new EpochSearcher(
		exe,
		searcher_base->createEmpty(),
		global_pool->createEmpty()); }
	void update(ExecutionState *current, States s);
	bool empty(void) const { return global_pool->empty(); }
	void printName(std::ostream &os) const
	{
		os << "<EpochSearcher>\n";
		os << "<Base>";
		searcher_base->printName(os);
		os << "</Base>\n";
		os << "<Pool>\n";
		global_pool->printName(os);
		os << "</Pool>\n";
		os << "</EpochSearcher>\n";
	}
private:
	ExecutionState* selectPool(bool allowCompact);
	ExecutionState* selectEpoch(bool allowCompact);

	Searcher	*searcher_base;
	Searcher	*global_pool;
	uint64_t	last_cov;
	unsigned	pool_countdown;
	unsigned	pool_period;
	class Epoch {
	public:
		Epoch(Searcher* _s) : searcher(_s) {}
		virtual ~Epoch() { delete searcher;}
		void update(ExecutionState* current, const Searcher::States& s);
		void add(ExecutionState* s);
		Searcher* getSearcher(void) { return searcher; }
		unsigned getNumStates(void) const { return states.size(); }
	private:
		Searcher			*searcher;
		std::set<ExecutionState*>	states;
	};
	std::vector<Epoch*>	epochs;
	std::map<ExecutionState*, unsigned> es2epoch;
	unsigned		epoch_state_c;
	Executor		&exe;	/* for concretization */
};
}

#endif

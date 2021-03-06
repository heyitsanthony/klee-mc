#ifndef EPOCHSEARCHER_H
#define EPOCHSEARCHER_H

#include "../Core/Searcher.h"

namespace klee {
class EpochSearcher : public Searcher
{
public:
	EpochSearcher(
		Executor& _exe,
		Searcher* _searcher_base,
		Searcher* global_pool);

	Searcher* createEmpty(void) const override
	{ return new EpochSearcher(
		exe,
		searcher_base->createEmpty(),
		global_pool->createEmpty()); }

	ExecutionState *selectState(bool allowCompact) override;
	void update(ExecutionState *current, States s) override;
	void printName(std::ostream &os) const override {
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

	usearcher_t	searcher_base;
	usearcher_t	global_pool;
	uint64_t	last_cov;
	unsigned	pool_countdown;
	unsigned	pool_period;
	class Epoch {
	public:
		Epoch(Searcher* _s) : searcher(_s) {}
		virtual ~Epoch() = default;
		Epoch(Epoch&& e)
			: searcher(std::move(e.searcher))
			, states(std::move(e.states))
		{}
		void update(ExecutionState* current, const Searcher::States& s);
		void add(ExecutionState* s);
		Searcher* getSearcher(void) { return searcher.get(); }
		unsigned getNumStates(void) const { return states.size(); }
	private:
		usearcher_t	searcher;
		std::set<ExecutionState*>	states;
	};
	std::vector<Epoch>	epochs;
	std::map<ExecutionState*, unsigned> es2epoch;
	unsigned		epoch_state_c;
	Executor		&exe;	/* for concretization */
};
}

#endif

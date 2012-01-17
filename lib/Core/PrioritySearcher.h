#ifndef PRIORITYSEARCHER_H
#define PRIORITYSEARCHER_H

#include <queue>
#include "Searcher.h"
#include <iostream>

namespace klee
{

class Prioritizer
{
public:
	virtual ~Prioritizer() {}
	/* pr_k > pr_j => pr_k scheduled first */
	virtual int getPriority(ExecutionState& st) = 0;
	void latch(void) { latched = true; }
	void unlatch(void) { latched = false; }
	bool isLatched(void) const { return latched; }

protected:
	Prioritizer()
	: latched(false) {}
private:
	bool		latched;

};

class PrioritySearcher : public Searcher
{
public:
	ExecutionState &selectState(bool allowCompact);
	virtual ~PrioritySearcher(void) { delete prFunc; }

	void update(ExecutionState *current, States s);
	bool empty(void) const { return state_c == 0; }
	void printName(std::ostream &os) const { os << "PrioritySearcher\n"; }

private:
	bool refreshPriority(ExecutionState* es);
	void removeState(ExecutionState* es);
	void addState(ExecutionState* es);
	void clearDeadPriorities(void);

	class PrStates {
	public:
		PrStates(int pr)
		: priority(pr), next_state(0), used_states(0)
		{}

		~PrStates(void) {}
		int getPr(void) const { return priority; }
		unsigned int getStateCount(void) const { return used_states; }
		ExecutionState* getNext(void);

		unsigned addState(ExecutionState*);
		void rmvState(unsigned idx);
	private:
		int				priority;
		std::vector<ExecutionState*>	states;
		unsigned int			next_state;
		unsigned int			used_states;
	};

	struct PrStateCmp
	{int operator()(const PrStates* a, const PrStates* b)
	{ return (a->getPr() < b->getPr()); } };


	PrStates* getPrStates(int n);

	std::priority_queue<
		PrStates*,
		std::vector<PrStates*>,
		PrStateCmp>			pr_heap;
	typedef std::map<int, PrStates*>		prmap_ty;
	typedef std::pair<int /*pr*/, unsigned /*idx*/>	stateidx_ty;
	typedef std::map<ExecutionState*, stateidx_ty>	statemap_ty;

	PrStateCmp	cmp;
	statemap_ty	state_backmap;
	prmap_ty	priorities;
	unsigned int	state_c;
	Prioritizer	*prFunc;

public:
	PrioritySearcher(Prioritizer* p)
	: state_c(0)
	, prFunc(p)
	{}

};
}

#endif
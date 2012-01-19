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
	virtual Prioritizer* copy(void) const = 0;
#define DEFAULT_PR_COPY(x)	\
	virtual Prioritizer* copy(void) const { return new x (); }

protected:
	Prioritizer() : latched(false) {}
private:
	bool		latched;
};

class PrioritySearcher : public Searcher
{
public:
	ExecutionState &selectState(bool allowCompact);
	virtual ~PrioritySearcher(void) { delete prFunc; }

	virtual Searcher* createEmpty(void) const
	{ return new PrioritySearcher(
		prFunc->copy(), searcher_base->createEmpty()); }

	void update(ExecutionState *current, States s);
	bool empty(void) const { return state_c == 0; }
	void printName(std::ostream &os) const { os << "PrioritySearcher\n"; }

private:
	bool refreshPriority(ExecutionState* es);
	void demote(ExecutionState* es, int new_pr);
	void removeState(ExecutionState* es);
	void addState(ExecutionState* es);
	void clearDeadPriorities(void);

	typedef std::pair<int, Searcher*>	prsearcher_ty;

	struct PrCmp
	{int operator()(const prsearcher_ty& a, const prsearcher_ty& b)
	{ return (a.first < b.first); }};


	Searcher* getPrSearcher(int n);

	std::priority_queue<
		prsearcher_ty,
		std::vector<prsearcher_ty>,
		PrCmp>				pr_heap;
	typedef std::map<int, prsearcher_ty>	prmap_ty;
	typedef std::map<ExecutionState*, int>	statemap_ty;

	PrCmp	cmp;
	statemap_ty	state_backmap;
	prmap_ty	priorities;
	unsigned int	state_c;
	Prioritizer	*prFunc;
	Searcher	*searcher_base;
public:
	PrioritySearcher(Prioritizer* p, Searcher* base)
	: state_c(0)
	, prFunc(p)
	, searcher_base(base)
	{}

};
}

#endif
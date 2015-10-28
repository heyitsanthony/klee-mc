#ifndef PRIORITYSEARCHER_H
#define PRIORITYSEARCHER_H

#include <queue>
#include "../Core/Searcher.h"
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
	virtual void printName(std::ostream &os) const { os << "Prioritizer"; }
protected:
	Prioritizer() : latched(false) {}
private:
	bool		latched;
};

class AddPrioritizer : public Prioritizer
{
public:
	AddPrioritizer(Prioritizer* _l, Prioritizer* _r)
	: l(_l), r(_r) {}
	virtual ~AddPrioritizer(void) { delete l; delete r; }
	Prioritizer* copy(void) const
	{ return new AddPrioritizer(l->copy(), r->copy()); }
	virtual int getPriority(ExecutionState& st)
	{ return l->getPriority(st) + r->getPriority(st); }
	virtual void printName(std::ostream &os) const 
	{	os << "(AddPr "; l->printName(os);
		os << ' '; r->printName(os);
		os << ')'; }
private:
	Prioritizer	*l, *r;
};

class PrioritySearcher : public Searcher
{
public:
	ExecutionState *selectState(bool allowCompact);
	virtual ~PrioritySearcher(void)
	{
		delete prFunc;
		delete searcher_base;
	}

	virtual Searcher* createEmpty(void) const
	{ return new PrioritySearcher(
		prFunc->copy(), searcher_base->createEmpty()); }

	void update(ExecutionState *current, States s);
	bool empty(void) const { return state_c == 0; }
	void printName(std::ostream &os) const
	{
		os << "<PrioritySearcher pr=\"";
		prFunc->printName(os);
		os << "\">\n";
		searcher_base->printName(os);
		os << "\n</PrioritySearcher>\n";
	}

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

	statemap_ty	state_backmap;
	prmap_ty	priorities;
	unsigned int	state_c;
	Prioritizer	*prFunc;
	Searcher	*searcher_base;
	unsigned	pr_kick_rate;
public:
	PrioritySearcher(Prioritizer* p, Searcher* base);
	PrioritySearcher(Prioritizer* p, Searcher* base, unsigned kick_rate);
};
}

#endif
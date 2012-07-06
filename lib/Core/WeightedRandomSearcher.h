#ifndef WEIGHTEDRANDOMSEARCHER_H
#define WEIGHTEDRANDOMSEARCHER_H

#include "CoreStats.h"
#include "Searcher.h"

namespace klee
{
template<class T> class DiscretePDF;

class KBrInstruction;

class WeightFunc
{
public:
	virtual ~WeightFunc(void);
	const char* getName(void) const { return name; }
	virtual double weigh(const ExecutionState* es) const = 0;
	bool isUpdating(void) const { return updateWeights; }
	virtual WeightFunc* copy(void) const = 0;
protected:
	WeightFunc(const char* in_name, bool in_updateWeights)
	: name(in_name)
	, updateWeights(in_updateWeights) {}
private:
	const char	*name;
	bool		updateWeights;
};

#define DECL_WEIGHT(x,y) 		\
class x##Weight : public WeightFunc {	\
public:	\
	x##Weight() : WeightFunc(#x, y), exe(NULL) { } \
	x##Weight(Executor* _exe) : WeightFunc(#x, y), exe(_exe) {} \
	virtual double weigh(const ExecutionState* es) const;	\
	virtual WeightFunc* copy(void) const { return new x##Weight(exe); }\
	virtual ~x##Weight() {} \
protected: \
	Executor	*exe; };

DECL_WEIGHT(Depth, false)
DECL_WEIGHT(QueryCost, true)
DECL_WEIGHT(PerInstCount, true)
DECL_WEIGHT(CPInstCount, true)
DECL_WEIGHT(MinDistToUncovered, true)
DECL_WEIGHT(CoveringNew, true)
DECL_WEIGHT(MarkovPath, true)
DECL_WEIGHT(Tail, true)
DECL_WEIGHT(Constraint, true)
DECL_WEIGHT(FreshBranch, true)
DECL_WEIGHT(StateInstCount, true)
DECL_WEIGHT(CondSucc, true)
DECL_WEIGHT(Uncov, true)

class Executor;

class TroughWeight : public WeightFunc
{
public:
	TroughWeight(Executor* _exe, unsigned _tw = 10000)
	: WeightFunc("Trough", true)
	, exe(_exe)
	, trough_width(_tw)
	{ last_ins = stats::instructions; }
	virtual ~TroughWeight() {}

	virtual double weigh(const ExecutionState* es) const;
	virtual WeightFunc* copy(void) const
	{ return new TroughWeight(exe, trough_width); }
private:
	Executor				*exe;
	unsigned				trough_width;
	mutable std::map<unsigned, unsigned>	trough_hits;
	mutable uint64_t			last_ins;
};

class FrontierTroughWeight : public WeightFunc
{
public:
	FrontierTroughWeight(Executor* _exe, unsigned _tw = 10000)
	: WeightFunc("FrontierTrough", true)
	, exe(_exe)
	, trough_width(_tw)
	{ last_ins = stats::instructions; }
	virtual ~FrontierTroughWeight() {}

	virtual double weigh(const ExecutionState* es) const;
	virtual WeightFunc* copy(void) const
	{ return new FrontierTroughWeight(exe, trough_width); }
private:
	Executor			*exe;
	unsigned			trough_width;
	typedef std::map<unsigned, std::set<unsigned>* >
		trough_hit_ty;
	mutable trough_hit_ty		trough_hits;
	mutable uint64_t		last_ins;
};



class BranchWeight : public WeightFunc
{
public:
	BranchWeight(Executor* _exe, unsigned _nw = 10000)
	: WeightFunc("BranchInsWeight", true)
	, exe(_exe)
	, n_width(_nw) /* neighborhood width */
	{ last_ins = stats::instructions; }
	virtual ~BranchWeight() {}

	virtual double weigh(const ExecutionState* es) const;
	virtual WeightFunc* copy(void) const
	{ return new BranchWeight(exe, n_width); }
private:
	void loadIns(void) const;
	unsigned getBrCount(uint64_t insts) const;
	unsigned getStCount(uint64_t insts) const;

	Executor			*exe;
	unsigned			n_width;	/* neighborhood width */
	typedef std::map<uint64_t, const KBrInstruction* > br_ins_ty;
	typedef std::map<uint64_t, const ExecutionState* > st_ins_ty;

	/* branches keyed by instruction visit */
	mutable br_ins_ty		br_ins;
	mutable st_ins_ty		st_ins; /* state instructions */
	mutable uint64_t		last_ins;
};


class WeightedRandomSearcher : public Searcher
{
private:
	Executor			&executor;
	DiscretePDF<ExecutionState*>	*pdf;
	WeightFunc			*weigh_func;
	double getWeight(ExecutionState*);

public:
	WeightedRandomSearcher(Executor &executor, WeightFunc* wf);
	virtual ~WeightedRandomSearcher();
	virtual Searcher* createEmpty(void) const
	{ return new WeightedRandomSearcher(executor, weigh_func->copy()); }

	ExecutionState &selectState(bool allowCompact);
	void update(ExecutionState *current, const States s);
	bool empty() const;
	void printName(std::ostream &os) const
	{
		os	<< "WeightedRandomSearcher::"
			<< weigh_func->getName() << "\n";
	}
};
}

#endif

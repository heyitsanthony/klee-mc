#ifndef WEIGHTEDRANDOMSEARCHER_H
#define WEIGHTEDRANDOMSEARCHER_H

#include "Searcher.h"

namespace klee
{
template<class T> class DiscretePDF;

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
	x##Weight() : WeightFunc(#x, y) {}	\
	virtual double weigh(const ExecutionState* es) const;	\
	virtual WeightFunc* copy(void) const { return new x##Weight(); }\
	virtual ~x##Weight() {} };

DECL_WEIGHT(Depth, false)
DECL_WEIGHT(QueryCost, true)
DECL_WEIGHT(InstCount, true)
DECL_WEIGHT(CPInstCount, true)
DECL_WEIGHT(MinDistToUncovered, true)
DECL_WEIGHT(CoveringNew, true)

class WeightedRandomSearcher : public Searcher
{
private:
	Executor			&executor;
	DiscretePDF<ExecutionState*>	*states;
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

#ifndef WEIGHTEDRANDOMSEARCHER_H
#define WEIGHTEDRANDOMSEARCHER_H

#include "Searcher.h"

namespace klee
{
template<class T> class DiscretePDF;

class WeightedRandomSearcher : public Searcher
{
public:
	enum WeightType {
	Depth,
	QueryCost,
	InstCount,
	CPInstCount,
	MinDistToUncovered,
	CoveringNew
	};

private:
	Executor &executor;
	DiscretePDF<ExecutionState*> *states;
	WeightType type;
	bool updateWeights;        
	double getWeight(ExecutionState*);

public:
	WeightedRandomSearcher(Executor &executor, WeightType type);
	virtual ~WeightedRandomSearcher();
	ExecutionState &selectState(bool allowCompact);
	void update(ExecutionState *current, const States s);
	bool empty() const;
	void printName(std::ostream &os) const
	{
		os << "WeightedRandomSearcher::";
		switch(type) {
		case Depth:		os << "Depth\n"; return;
		case QueryCost:		os << "QueryCost\n"; return;
		case InstCount:		os << "InstCount\n"; return;
		case CPInstCount:	os << "CPInstCount\n"; return;
		case MinDistToUncovered:os << "MinDistToUncovered\n"; return;
		case CoveringNew:	os << "CoveringNew\n"; return;
		default:		os << "<unknown type>\n"; return;
		}
	}
};
}

#endif

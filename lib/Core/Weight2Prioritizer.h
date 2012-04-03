#ifndef WEIGHT2PRIORITY_H
#define WEIGHT2PRIORITY_H

#include "PrioritySearcher.h"
#include "WeightedRandomSearcher.h"

namespace klee
{

template <class T>
class Weight2Prioritizer : public Prioritizer
{
public:
	Weight2Prioritizer(T* t, double _s) : w(t), scale(_s) {}
	Weight2Prioritizer(double _s) : w(new T()), scale(_s) {}
	virtual ~Weight2Prioritizer() { delete w; }

	virtual Prioritizer* copy(void) const
	{ return new Weight2Prioritizer<T>(static_cast<T*>(w), scale); }
	/* pr_k > pr_j => pr_k scheduled first */
	virtual int getPriority(ExecutionState& st) { return scale*w->weigh(&st); }
	virtual void printName(std::ostream &os) const
	{ os << "Pr("<<w->getName()<<")"; }
private:
	WeightFunc	*w;
	double		scale;
};

}
#endif

#ifndef INDEPSOLVER_H
#define INDEPSOLVER_H

#include "klee/Solver.h"
#include "SolverImplWrapper.h"
#include "klee/Internal/ADT/RNG.h"

namespace klee
{
class IndependentSolver : public SolverImplWrapper
{
public:
	IndependentSolver(Solver *_solver)
	: SolverImplWrapper(_solver)
	{ rng.seed(12345); }

	virtual ~IndependentSolver() {}

	bool computeSat(const Query&);
	Solver::Validity computeValidity(const Query&);
	ref<Expr> computeValue(const Query&);
	bool computeInitialValues(const Query& query, Assignment& a)
	{ return doComputeInitialValues(query, a); }

	void printName(int level = 0) const
	{
		klee_message("%*s""IndependentSolver containing:", 2*level, "");
		wrappedSolver->printName(level + 1);
	}

	static uint64_t getIndependentCount(void) { return indep_c; }
private:
	static uint64_t	indep_c;

	RNG rng;
};
}

#endif

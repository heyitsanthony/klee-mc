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

	bool computeSat(const Query&) override;
	Solver::Validity computeValidity(const Query&) override;
	ref<Expr> computeValue(const Query&) override;
	bool computeInitialValues(const Query& query, Assignment& a) override
	{ return doComputeInitialValues(query, a); }

	void printName(int level = 0) const override
	{
		klee_message("%*s""IndependentSolver containing:", 2*level, "");
		wrappedSolver->printName(level + 1);
	}

	static uint64_t getIndependentCount(void) { return indep_c; }
	static Query getIndependentQuery(const Query& q, ConstraintManager& cs);
private:
	static uint64_t	indep_c;

	RNG rng;
};
}

#endif

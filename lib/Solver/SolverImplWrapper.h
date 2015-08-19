#ifndef KLEE_SOLVERWRAPPER_H
#define KLEE_SOLVERWRAPPER_H

#include "klee/Solver.h"
#include "SolverImpl.h"

namespace klee
{
/* wraps a solverimpl so failures can be nicely propagated */
class SolverImplWrapper : public SolverImpl
{
public:
	SolverImplWrapper(Solver* _solver) : wrappedSolver(_solver) {}
	virtual ~SolverImplWrapper() = default;

protected:
	void failQuery(void) override;

	bool doComputeSat(const Query& q);
	ref<Expr> doComputeValue(const Query& q);
	Solver::Validity doComputeValidity(const Query& q);
	Solver::Validity doComputeValiditySplit(const Query& q);
	bool doComputeInitialValues(const Query& query, Assignment&);

	std::unique_ptr<Solver>	wrappedSolver;
private:
};
}
#endif

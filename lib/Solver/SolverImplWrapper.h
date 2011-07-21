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
	virtual ~SolverImplWrapper(void);

protected:
	virtual void failQuery(void);
	bool doComputeSat(const Query& q);
	ref<Expr> doComputeValue(const Query& q);
	Solver::Validity doComputeValidity(const Query& q);
	bool doComputeInitialValues(
		const Query& query,
		const std::vector<const Array*> &objects,
		std::vector< std::vector<unsigned char> > &values);

	Solver*	wrappedSolver;
private:
};
}
#endif

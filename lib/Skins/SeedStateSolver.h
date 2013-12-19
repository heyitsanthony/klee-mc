#ifndef KLEE_SEEDSTATESOLVER_H
#define KLEE_SEEDSTATESOLVER_H

#include "../Core/StateSolver.h"

namespace klee
{
class Executor;
class ExecutionState;
class Solver;

class SeedStateSolver : public StateSolver
{
public:
	SeedStateSolver(
		Executor& _exe,
		Solver *_solver,
		TimedSolver *_timedSolver,
		bool _simplifyExprs = true)
	: StateSolver(_solver, _timedSolver, _simplifyExprs)
	, exe(_exe)
	{}

	virtual ~SeedStateSolver() {}

	bool evaluate(const ExecutionState&, ref<Expr>, Solver::Validity &result);
	bool mustBeTrue(const ExecutionState&, ref<Expr>, bool &result);
	bool mustBeFalse(const ExecutionState&, ref<Expr>, bool &result);
	bool mayBeTrue(const ExecutionState&, ref<Expr>, bool &result);
	bool mayBeFalse(const ExecutionState&, ref<Expr>, bool &result);
	bool getValue(
		const ExecutionState &es,
		ref<Expr> expr,
		ref<ConstantExpr> &result,
		ref<Expr> predicate = 0);

	bool getInitialValues(const ExecutionState&, Assignment&);


	bool getRange(
		const ExecutionState&,
		ref<Expr> query,
		std::pair< ref<Expr>, ref<Expr> >& ret);

	ref<Expr> toUnique(const ExecutionState &state, const ref<Expr> &e);

private:
	Executor	&exe;
};
}
#endif

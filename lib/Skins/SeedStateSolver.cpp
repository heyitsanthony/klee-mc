#include "../Core/Executor.h"
#include "../Core/Forks.h"
#include "SeedStateSolver.h"

using namespace klee;

bool SeedStateSolver::evaluate(
	const ExecutionState& es,
	ref<Expr> e, Solver::Validity &result)
{
	exe.getForking()->setConstraintOmit(true);
	return StateSolver::evaluate(es, e, result);
}

bool SeedStateSolver::mustBeTrue(
	const ExecutionState& es, ref<Expr> e, bool &result)
{
	exe.getForking()->setConstraintOmit(true);
	return StateSolver::mustBeTrue(es, e, result);
}

bool SeedStateSolver::mustBeFalse(
	const ExecutionState& es, ref<Expr> e, bool &result)
{
	exe.getForking()->setConstraintOmit(true);
	return StateSolver::mustBeFalse(es, e, result);
}

bool SeedStateSolver::mayBeTrue(
	const ExecutionState& es, ref<Expr> e, bool &result)
{
	exe.getForking()->setConstraintOmit(true);
	return StateSolver::mayBeTrue(es, e, result);
}


bool SeedStateSolver::mayBeFalse(
	const ExecutionState& es, ref<Expr> e, bool &result)
{
	exe.getForking()->setConstraintOmit(true);
	return StateSolver::mayBeFalse(es, e, result);
}

bool SeedStateSolver::getValue(
	const ExecutionState &es,
	ref<Expr> expr,
	ref<ConstantExpr> &result)
{
	exe.getForking()->setConstraintOmit(true);
	return StateSolver::getValue(es, expr, result);
}

bool SeedStateSolver::getInitialValues(
	const ExecutionState& es, Assignment& a)
{
	exe.getForking()->setConstraintOmit(true);
	return StateSolver::getInitialValues(es, a);
}

bool SeedStateSolver::getRange(
	const ExecutionState& es,
	ref<Expr> query,
	std::pair< ref<Expr>, ref<Expr> >& ret)
{
	exe.getForking()->setConstraintOmit(true);
	return StateSolver::getRange(es, query, ret);
}

ref<Expr> SeedStateSolver::toUnique(const ExecutionState &es, ref<Expr> &e)
{
	exe.getForking()->setConstraintOmit(true);
	return StateSolver::toUnique(es, e);
}

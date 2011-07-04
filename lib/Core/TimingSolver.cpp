//===-- TimingSolver.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TimingSolver.h"

#include "klee/ExecutionState.h"
#include "klee/Solver.h"
#include "klee/Statistics.h"
#include "klee/Internal/System/Time.h"

#include "CoreStats.h"

#include "llvm/System/Process.h"

using namespace klee;
using namespace llvm;

/***/

#define SOLVER_INIT							\
	double	start, finish;						\
	bool	success;						\
	/* Fast path, to avoid timer and OS overhead. */		\
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {		\
		result = CE->isTrue() ? Solver::True : Solver::False;	\
		return true;						\
	}								\
	start = util::estWallTime();					\
	if (simplifyExprs) expr = state.constraints.simplifyExpr(expr);

#define SOLVER_FINI							\
	finish = util::estWallTime();					\
	stats::solverTime += (std::max(0.,finish - start)) * 1000000.;	\
	state.queryCost += (std::max(0.,finish - start));

bool TimingSolver::evaluate(
	const ExecutionState& state,
	ref<Expr> expr,
	Solver::Validity &result)
{
	SOLVER_INIT
	success = solver->evaluate(Query(state.constraints, expr), result);
	SOLVER_FINI

	return success;
}

bool TimingSolver::mustBeTrue(
	const ExecutionState& state,
	ref<Expr> expr,
	bool &result)
{
	SOLVER_INIT
	success = solver->mustBeTrue(Query(state.constraints, expr), result);
	SOLVER_FINI

	return success;
}

bool TimingSolver::getValue(
	const ExecutionState& state,
	ref<Expr> expr,
	ref<ConstantExpr> &result)
{
	double	start, finish;
	bool	success;

	// Fast path, to avoid timer and OS overhead.
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
		result = CE;
		return true;
	}

	start = util::estWallTime();
	if (simplifyExprs)
		expr = state.constraints.simplifyExpr(expr);

	success = solver->getValue(Query(state.constraints, expr), result);

	SOLVER_FINI

	return success;
}

bool TimingSolver::mustBeFalse(const ExecutionState& state, ref<Expr> expr,
                               bool &result) {
  return mustBeTrue(state, Expr::createIsZero(expr), result);
}

bool TimingSolver::mayBeTrue(const ExecutionState& state, ref<Expr> expr,
                             bool &result) {
  bool res;
  if (!mustBeFalse(state, expr, res))
    return false;
  result = !res;
  return true;
}

bool TimingSolver::mayBeFalse(const ExecutionState& state, ref<Expr> expr,
                              bool &result) {
  bool res;
  if (!mustBeTrue(state, expr, res))
    return false;
  result = !res;
  return true;
}

bool TimingSolver::getInitialValues(
	const ExecutionState& state,
        const std::vector<const Array*> &objects,
	std::vector< std::vector<unsigned char> > &result)
{
	double		start, finish;
	bool		success;

	if (objects.empty())
		return true;

	start = util::estWallTime();
	success = solver->getInitialValues(
		Query(	state.constraints,
			ConstantExpr::alloc(0, Expr::Bool)),
		objects,
		result);

	SOLVER_FINI

	return success;
}

std::pair< ref<Expr>, ref<Expr> >
TimingSolver::getRange(const ExecutionState& state, ref<Expr> expr) {
  return solver->getRange(Query(state.constraints, expr));
}

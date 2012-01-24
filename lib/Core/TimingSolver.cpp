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
#include "klee/util/Assignment.h"

#include "CoreStats.h"

#include "llvm/Support/Process.h"

using namespace klee;
using namespace llvm;

/***/

bool TimingSolver::evaluate(const ExecutionState& state, ref<Expr> expr,
                            Solver::Validity &result) {
  // Fast path, to avoid timer and OS overhead.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
    result = CE->isTrue() ? Solver::True : Solver::False;
    return true;
  }

  double start = util::estWallTime();

  if (simplifyExprs)
    expr = state.constraints.simplifyExpr(expr);

  bool success = solver->evaluate(Query(state.constraints, expr), result);

  double finish = util::estWallTime();
  stats::solverTime += (std::max(0.,finish - start)) * 1000000.;
  state.queryCost += (std::max(0.,finish - start));

  return success;
}

bool TimingSolver::mustBeTrue(const ExecutionState& state, ref<Expr> expr,
                              bool &result) {
  // Fast path, to avoid timer and OS overhead.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
    result = CE->isTrue() ? true : false;
    return true;
  }

  double start = util::estWallTime();

  if (simplifyExprs)
    expr = state.constraints.simplifyExpr(expr);

  bool success = solver->mustBeTrue(Query(state.constraints, expr), result);

  double finish = util::estWallTime();
  stats::solverTime += (std::max(0.,finish - start)) * 1000000.;
  state.queryCost += (std::max(0.,finish - start));

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

bool TimingSolver::getValue(const ExecutionState& state, ref<Expr> expr,
                            ref<ConstantExpr> &result) {
  // Fast path, to avoid timer and OS overhead.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
    result = CE;
    return true;
  }

  double start = util::estWallTime();

  if (simplifyExprs)
    expr = state.constraints.simplifyExpr(expr);

  bool success = solver->getValue(Query(state.constraints, expr), result);

  double finish = util::estWallTime();
  stats::solverTime += (std::max(0.,finish - start)) * 1000000.;
  state.queryCost += (std::max(0.,finish - start));

  return success;
}

bool TimingSolver::getInitialValues(
	const ExecutionState& state, Assignment& a)
{
  if (a.getNumFree() == 0)
    return true;

  double start = util::estWallTime();

  bool success;
  success = solver->getInitialValues(
  	Query(
		state.constraints,
                ConstantExpr::alloc(0, Expr::Bool)),
	a);

  double finish = util::estWallTime();
  stats::solverTime += (std::max(0.,finish - start)) * 1000000.;
  state.queryCost += (std::max(0.,finish - start));

  return success;
}

bool TimingSolver::getRange(
	const ExecutionState& state,
	ref<Expr> expr,
	std::pair< ref<Expr>, ref<Expr> >& ret)
{ return solver->getRange(Query(state.constraints, expr), ret); }

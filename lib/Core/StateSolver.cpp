//===-- StateSolver.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "StateSolver.h"

#include "klee/ExecutionState.h"
#include "klee/Solver.h"
#include "klee/Statistics.h"
#include "klee/Internal/System/Time.h"
#include "klee/util/Assignment.h"

#include "CoreStats.h"
#include "klee/SolverStats.h"

using namespace klee;
using namespace llvm;

uint64_t StateSolver::constQueries = 0;

#define WRAP_QUERY(x)	\
do {	\
	start = util::getWallTime();	\
	if (simplifyExprs)	\
		expr = state.constraints.simplifyExpr(expr);	\
	ok = x;	\
	finish = util::getWallTime();	\
	stats::solverTime += (std::max(0.,finish - start)) * 1000000.;	\
	state.queryCost += (std::max(0.,finish - start));	\
} while (0)

#define WRAP_QUERY_NOSIMP(x)	\
do {	\
	start = util::getWallTime();	\
	ok = x;	\
	finish = util::getWallTime();	\
	stats::solverTime += (std::max(0.,finish - start)) * 1000000.;	\
	state.queryCost += (std::max(0.,finish - start));	\
} while (0)


uint64_t StateSolver::getRealQueries(void)
{ return stats::queriesTopLevel - constQueries;	}

bool StateSolver::evaluate(
	const ExecutionState& state,
	ref<Expr> expr,
	Solver::Validity &result)
{
	double	start, finish;
	bool	ok;

	++stats::queriesTopLevel;

	// Fast path, to avoid timer and OS overhead.
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
		result = CE->isTrue() ? Solver::True : Solver::False;
		constQueries++;
		return true;
	}

	WRAP_QUERY(solver->evaluate(Query(state.constraints, expr), result));

	return ok;
}

bool StateSolver::mustBeTrue(
	const ExecutionState& state, ref<Expr> expr, bool &result)
{
	double	start, finish;
	bool	ok;

	++stats::queriesTopLevel;

	// Fast path, to avoid timer and OS overhead.
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
		result = CE->isTrue() ? true : false;
		constQueries++;
		return true;
	}


	WRAP_QUERY(solver->mustBeTrue(Query(state.constraints, expr), result));

	return ok;
}

bool StateSolver::mustBeFalse(
	const ExecutionState& state, ref<Expr> expr, bool &result)
{ return mustBeTrue(state, Expr::createIsZero(expr), result); }

bool StateSolver::mayBeTrue(
	const ExecutionState& state, ref<Expr> expr, bool &result)
{
	bool res;
	if (mustBeFalse(state, expr, res) == false)
		return false;
	result = !res;
	return true;
}

bool StateSolver::mayBeFalse(
	const ExecutionState& state, ref<Expr> expr, bool &result)
{
	bool res;
	if (!mustBeTrue(state, expr, res))
		return false;
	result = !res;
	return true;
}

bool StateSolver::getValue(
	const ExecutionState& state, ref<Expr> expr, ref<ConstantExpr> &result)
{
	double	start, finish;
	bool	ok;

	++stats::queriesTopLevel;

	// Fast path, to avoid timer and OS overhead.
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
		result = CE;
		constQueries++;
		return true;
	}

	WRAP_QUERY(solver->getValue(Query(state.constraints, expr), result));

	return ok;
}

bool StateSolver::getInitialValues(const ExecutionState& state, Assignment& a)
{
	bool	ok;
	double	start, finish;

	++stats::queriesTopLevel;
	if (a.getNumFree() == 0)
		return true;

	WRAP_QUERY_NOSIMP(solver->getInitialValues(
		Query(	state.constraints,
			ConstantExpr::create(0, Expr::Bool)),
		a));

	return ok;
}

bool StateSolver::getRange(
	const ExecutionState& state,
	ref<Expr> expr,
	std::pair< ref<Expr>, ref<Expr> >& ret)
{ return solver->getRange(Query(state.constraints, expr), ret); }

ref<Expr> StateSolver::toUnique(const ExecutionState &state, ref<Expr> &e)
{
	ref<ConstantExpr>	value;
	ref<Expr>		eq_expr;
	bool			isTrue = false;

	if (isa<ConstantExpr>(e))
		return e;

	if (!getValue(state, e, value))
		return e;

	eq_expr = EqExpr::create(e, value);
	if (!mustBeTrue(state, eq_expr, isTrue))
		return e;

	if (isTrue)
		return value;

	return e;
}

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
#include <string.h>

using namespace klee;
using namespace llvm;

uint64_t StateSolver::constQueries = 0;
unsigned StateSolver::timeBuckets[NUM_STATESOLVER_BUCKETS];
double StateSolver::timeBucketTotal[NUM_STATESOLVER_BUCKETS];

extern double MaxSTPTime;

#define WRAP_QUERY(x)	\
do {	\
	start = util::getWallTime();	\
	if (simplifyExprs)	\
		expr = state.constraints.simplifyExpr(expr);	\
	ok = x;	\
	finish = util::getWallTime();	\
	if (!updateTimes(state, std::max(0.,finish - start))) ok = false; \
} while (0)

#define WRAP_QUERY_NOSIMP(x)	\
do {	\
	start = util::getWallTime();	\
	ok = x;	\
	finish = util::getWallTime();	\
	if (!updateTimes(state, std::max(0.,finish - start))) ok = false; \
} while (0)


StateSolver::StateSolver(
	Solver *_solver,
	TimedSolver *_timedSolver,
	bool _simplifyExprs)
: solver(_solver)
, timedSolver(_timedSolver)
, simplifyExprs(_simplifyExprs)
{
	memset(timeBuckets, 0, sizeof(timeBuckets));
	memset(timeBucketTotal, 0, sizeof(timeBucketTotal));
}

uint64_t StateSolver::getRealQueries(void)
{ return stats::queriesTopLevel - constQueries;	}

bool StateSolver::updateTimes(const ExecutionState& state, double totalTime)
{
	double	timeLowest = STATESOLVER_LOWEST_TIME;
	/* convert to milliseconds */
	stats::solverTime += totalTime * 1.0e6;
	state.queryCost += totalTime;

	for (unsigned i = 0; i < NUM_STATESOLVER_BUCKETS; i++) {
		if (totalTime < timeLowest) {
			timeBuckets[i]++;
			timeBucketTotal[i] += totalTime;
			break;
		}
		timeLowest *= STATESOLVER_TIME_INTERVAL;
	}

	return totalTime < MaxSTPTime;
}

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
		Query(state.constraints, MK_CONST(0, Expr::Bool)),
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

	eq_expr = MK_EQ(e, value);
	if (!mustBeTrue(state, eq_expr, isTrue))
		return e;

	if (isTrue)
		return value;

	return e;
}

void StateSolver::dumpTimes(std::ostream& os)
{
	double	timeLowest = STATESOLVER_LOWEST_TIME;
	os << "# LT-TIME NUM-QUERIES TOTAL-TIME\n";
	for (unsigned i = 0; i < NUM_STATESOLVER_BUCKETS; i++) {
		os	<< timeLowest << ' ' 
			<< timeBuckets[i] << ' '
			<< timeBucketTotal[i] << '\n';
		timeLowest *= STATESOLVER_TIME_INTERVAL;
	}
}

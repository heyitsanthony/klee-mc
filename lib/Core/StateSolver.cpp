//===-- StateSolver.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "StateSolver.h"

#include <llvm/Support/CommandLine.h>

#include "klee/ExecutionState.h"
#include "klee/Solver.h"
#include "klee/Statistics.h"
#include "klee/Internal/System/Time.h"
#include "klee/util/Assignment.h"

#include "CoreStats.h"
#include "../Solver/SMTPrinter.h"
#include "../Solver/IndependentSolver.h"
#include "klee/SolverStats.h"
#include <string.h>

using namespace klee;
using namespace llvm;

uint64_t StateSolver::constQueries = 0;
unsigned StateSolver::timeBuckets[NUM_STATESOLVER_BUCKETS];
double StateSolver::timeBucketTotal[NUM_STATESOLVER_BUCKETS];
#define TAG "[StateSolver] "

extern double MaxSTPTime;

namespace {
llvm::cl::opt<bool> ChkGetValue("chk-getvalue", cl::init(false));
}

#define WRAP_QUERY(x)	\
do {	\
	start = util::getWallTime();	\
	if (simplifyExprs)	\
		expr = state.constraints.simplifyExpr(expr);	\
	ok = x;	\
	finish = util::getWallTime();	\
	if (!updateTimes(state, std::max(0.,finish - start))) ok = false; \
	if (ok == false) last_bad = expr; \
} while (0)

#define WRAP_QUERY_NOSIMP(x)	\
do {	\
	start = util::getWallTime();	\
	ok = x;	\
	finish = util::getWallTime();	\
	if (!updateTimes(state, std::max(0.,finish - start))) ok = false; \
	if (ok == false) last_bad = NULL; \
} while (0)

// includes fast path, to avoid timer and OS overhead
#define SETUP_QUERY			\
	double	start, finish;		\
	bool	ok;			\
	++stats::queriesTopLevel;	\
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr))

#define SETUP_TRUTH_QUERY	\
	SETUP_QUERY {		\
		result = CE->isTrue() ? true : false;	\
		constQueries++;	\
		return true; }	\


StateSolver::StateSolver(Solver *_solver, TimedSolver *_timedSolver, bool _simplifyExprs)
: solver(_solver)
, timedSolver(_timedSolver)
, simplifyExprs(_simplifyExprs)
{
	assert (solver != nullptr && "state solver without a solver??");
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

	return MaxSTPTime == 0.0 || (totalTime < MaxSTPTime);
}

bool StateSolver::evaluate(
	const ExecutionState& state,
	ref<Expr> expr,
	Solver::Validity &result)
{
	SETUP_QUERY {
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
	SETUP_TRUTH_QUERY
	WRAP_QUERY(solver->mustBeTrue(Query(state.constraints, expr), result));
	return ok;
}

bool StateSolver::mustBeFalse(
	const ExecutionState& state, ref<Expr> expr, bool &result)
{ return mustBeTrue(state, Expr::createIsZero(expr), result); }

bool StateSolver::mayBeTrue(
	const ExecutionState& state, ref<Expr> expr, bool &result)
{
	SETUP_TRUTH_QUERY
	WRAP_QUERY(solver->mayBeTrue(Query(state.constraints, expr), result));
	return ok;
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

static void chk_getvalue(
	StateSolver *ss,
	const ExecutionState& state,
	ref<Expr>& expr, ref<ConstantExpr>& result)
{
	ConstraintManager cm;
	ref<Expr>	cond(MK_EQ(result, expr));
	bool		mbt = false, ok;

	std::cerr << TAG"Checking GetValue equality...\n";
	if ((ok = ss->mayBeTrue(state, cond, mbt)) && mbt) {
		// query failed or query is satisfiable
		std::cerr << TAG"Equality OK\n";
		return;
	}

	if (!ok) {
		std::cerr << TAG"GetValue check failed to complete...\n";
	}

	std::cerr << TAG"Dumping queries.\n";

	ss->getSolver()->printName();
	SMTPrinter::dump(Query(state.constraints, cond), "eqchk-fail");

	IndependentSolver::getIndependentQuery(Query(state.constraints, cond), cm);
	SMTPrinter::dump(Query(cm, cond), "eqchk-fail-indep");

	if (!ok) {
		std::cerr << TAG"try one more time\n";
		ok = ss->mayBeTrue(state, cond, mbt);
		assert (!ok);
		return;
	}

	assert (mbt);
}

bool StateSolver::getValue(
	const ExecutionState& state,
	ref<Expr> expr,
	ref<ConstantExpr> &result,
	ref<Expr> predicate)
{
	SETUP_QUERY {
		result = CE;
		constQueries++;
		return true;
	}

	if (predicate.isNull()) {
		Query	q(state.constraints, expr);
		WRAP_QUERY(solver->getValue(
			Query(state.constraints, expr), result));
	} else {
		ConstraintManager	cm(state.constraints);
		cm.addConstraint(predicate);
		WRAP_QUERY(solver->getValue(Query(cm, expr), result));
	}

	if (	ok &&
		ChkGetValue &&
		(predicate.isNull() || predicate->getKind()==Expr::Constant))
	{
		std::cerr  << TAG"Got value " << result << '\n';
		chk_getvalue(this, state, expr, result);
		std::cerr  << TAG"Value OK!\n";
	}

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

ref<Expr> StateSolver::toUnique(
	const ExecutionState &state, const ref<Expr> &e)
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

//===-- StateSolver.h ------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_STATESOLVER_H
#define KLEE_STATESOLVER_H

#include <iostream>
#include "klee/Expr.h"
#include "klee/Solver.h"

namespace klee
{
class ExecutionState;
class Solver;

/// StateSolver - A simple class which wraps a solver and handles
/// tracking the statistics that we care about.
class StateSolver
{
protected:
	std::unique_ptr<Solver>		solver;
	TimedSolver			*timedSolver; // kept for setTimeout
public:
	bool		simplifyExprs;

	/// \param _simplifyExprs - Whether expressions should be
	/// simplified (via the constraint manager interface) prior to
	/// querying.
	StateSolver(
		Solver *_solver,
		TimedSolver *_timedSolver,
		bool _simplifyExprs = true);

	virtual ~StateSolver() = default;

	void setTimeout(double t) { timedSolver->setTimeout(t); }
	virtual Solver *getSolver(void) { return solver.get(); }
	virtual TimedSolver *getTimedSolver(void) { return timedSolver; }

	virtual bool evaluate(
		const ExecutionState&, ref<Expr>, Solver::Validity &result);
	virtual bool mustBeTrue(
		const ExecutionState&, ref<Expr>, bool &result);
	virtual bool mustBeFalse(
		const ExecutionState&, ref<Expr>, bool &result);
	virtual bool mayBeTrue(
		const ExecutionState&, ref<Expr>, bool &result);
	virtual bool mayBeFalse(const ExecutionState&, ref<Expr>, bool &result);
	virtual bool getValue(
		const ExecutionState &,
		ref<Expr> expr,
		ref<ConstantExpr> &result,
		ref<Expr> predicate = 0);

	virtual bool getInitialValues(const ExecutionState&, Assignment&);
	virtual bool getRange(
		const ExecutionState&,
		ref<Expr> query,
		std::pair< ref<Expr>, ref<Expr> >& ret);

	/// Return a unique constant value for the given expression in the
	/// given state, if it has one (i.e. it provably only has a single
	/// value). Otherwise return the original expression.
	virtual ref<Expr> toUnique(
		const ExecutionState &state,
		const ref<Expr> &e);

	static uint64_t getConstQueries(void) { return constQueries; }
	static uint64_t getRealQueries(void);

	static void dumpTimes(std::ostream& os);

	ref<Expr> getLastBadExpr(void) const { return last_bad; }
private:
	static uint64_t	constQueries;
	bool updateTimes(const ExecutionState& state, double totalTime);
#define STATESOLVER_LOWEST_TIME		1.0e-6
#define STATESOLVER_TIME_INTERVAL	1.5
#define NUM_STATESOLVER_BUCKETS		45
	static unsigned timeBuckets[NUM_STATESOLVER_BUCKETS];
	static double timeBucketTotal[NUM_STATESOLVER_BUCKETS];
	ref<Expr>	last_bad;
};
}

#endif

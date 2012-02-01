//===-- TimingSolver.h ------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_TIMINGSOLVER_H
#define KLEE_TIMINGSOLVER_H

#include "klee/Expr.h"
#include "klee/Solver.h"

#include <vector>

namespace klee {
class ExecutionState;
class Solver;
class STPSolver;

/// TimingSolver - A simple class which wraps a solver and handles
/// tracking the statistics that we care about.
class TimingSolver {
public:
	Solver *solver;
	TimedSolver *timedSolver;
	bool simplifyExprs;

	/// TimingSolver - Construct a new timing solver.
	///
	/// \param _simplifyExprs - Whether expressions should be
	/// simplified (via the constraint manager interface) prior to
	/// querying.
	TimingSolver(
		Solver *_solver,
		TimedSolver *_timedSolver,
		bool _simplifyExprs = true)
	: solver(_solver)
	, timedSolver(_timedSolver)
	, simplifyExprs(_simplifyExprs) {}

	~TimingSolver() { delete solver; }

	void setTimeout(double t) { timedSolver->setTimeout(t); }
	bool evaluate(const ExecutionState&, ref<Expr>, Solver::Validity &result);
	bool mustBeTrue(const ExecutionState&, ref<Expr>, bool &result);
	bool mustBeFalse(const ExecutionState&, ref<Expr>, bool &result);
	bool mayBeTrue(const ExecutionState&, ref<Expr>, bool &result);
	bool mayBeFalse(const ExecutionState&, ref<Expr>, bool &result);
	bool getValue(
		const ExecutionState &,
		ref<Expr> expr,
		ref<ConstantExpr> &result);

	bool getInitialValues(const ExecutionState&, Assignment&);


	bool getRange(
		const ExecutionState&,
		ref<Expr> query,
		std::pair< ref<Expr>, ref<Expr> >& ret);

	/// Return a unique constant value for the given expression in the
	/// given state, if it has one (i.e. it provably only has a single
	/// value). Otherwise return the original expression.
	ref<Expr> toUnique(const ExecutionState &state, ref<Expr> &e);
};

}

#endif

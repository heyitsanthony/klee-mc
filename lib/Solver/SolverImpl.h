//===-- SolverImpl.h --------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_SOLVERIMPL_H
#define KLEE_SOLVERIMPL_H

#include <iostream>
#include <vector>

namespace klee {
class Array;
class Assignment;
class ExecutionState;
class Expr;
class Query;

/// SolverImpl - Abstract base clase for solver implementations.
class SolverImpl {
	// DO NOT IMPLEMENT.
	SolverImpl(const SolverImpl&);
	void operator=(const SolverImpl&);

public:
	SolverImpl() : has_failed(false) {}
	virtual ~SolverImpl();

	/// computeValidity - Compute a full validity result for the
	/// query.
	///
	/// The query expression is guaranteed to be non-constant and have
	/// bool type.
	///
	/// SolverImpl provides a default implementation which uses
	/// computeTruth. Clients should override this if a more efficient
	/// implementation is available.
	virtual Solver::Validity computeValidity(const Query& query);

	/// computeTruth - Determine whether the given query is satisfiable.
	///
	/// The query expression is guaranteed to be non-constant and have
	/// bool type.
	virtual bool computeSat(const Query& query) = 0;

	/* finds a counter example for the given query
	* returns true if cex exists, false otherwise */
	virtual bool computeInitialValues(
		const Query& query, Assignment& a) = 0;

	/// computeValue - Compute a feasible value for the expression.
	/// Assumes that there is a solution to the given query.
	/// The query expression is guaranteed to be non-constant.
	virtual ref<Expr> computeValue(const Query& query);

	/// printName - Recursively print name of solver class
	virtual void printName(int level = 0) const = 0;
	virtual bool failed(void) const { return has_failed; }
	virtual void ackFail(void) { has_failed = false; }

protected:
	virtual void failQuery(void);
	bool has_failed;
};

}

#endif

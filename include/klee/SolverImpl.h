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
  class ExecutionState;
  class Expr;
  struct Query;

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
    virtual bool computeValidity(const Query& query, Solver::Validity &result);
    
    /// computeTruth - Determine whether the given query is provable.
    ///
    /// The query expression is guaranteed to be non-constant and have
    /// bool type.
    virtual bool computeTruth(const Query& query, bool &isSAT) = 0;

    /// computeValue - Compute a feasible value for the expression.
    /// Assumes that there is a solution to the given query.
    /// The query expression is guaranteed to be non-constant.
    virtual bool computeValue(const Query& query, ref<Expr> &result);
    
    virtual bool computeInitialValues(const Query& query,
                                      const std::vector<const Array*> 
                                        &objects,
                                      std::vector< std::vector<unsigned char> > 
                                        &values,
                                      bool &hasSolution) = 0;  

    /// printName - Recursively print name of solver class
    virtual void printName(int level = 0) const = 0;
    virtual bool failed(void) const { return has_failed; }

  protected:
    void printDebugQueries(
	std::ostream& os,
	double t_check,
	const std::vector<const Array*> &objects,
	std::vector< std::vector<unsigned char> > &values,
	bool hasSolution) const;
    void failQuery(void) { has_failed = true; }
    bool has_failed;
};

}

#endif

#ifndef DUMMYSOLVER_H
#define DUMMYSOLVER_H

#include "klee/Solver.h"
#include "SolverImpl.h"

namespace klee
{

class DummySolverImpl : public SolverImpl
{
public:
  DummySolverImpl() {}


  // FIXME: We should have stats::queriesFail;
  Solver::Validity computeValidity(const Query& q) {
    ++stats::queries;
    failQuery();
    return Solver::Unknown;
  }

  // FIXME: We should have stats::queriesFail;
  bool computeSat(const Query& q) {
    ++stats::queries;
    failQuery();
    return false;
  }

  ref<Expr> computeValue(const Query&) {
    ++stats::queries;
    ++stats::queryCounterexamples;
    failQuery();
    return NULL;
  }

  bool computeInitialValues(const Query&, Assignment& a)
  {
    ++stats::queries;
    ++stats::queryCounterexamples;
    failQuery();
    return false;
  }

  void printName(int level = 0) const {
    klee_message("%*s" "DummySolverImpl", 2*level, "");
  }
};

}

#endif

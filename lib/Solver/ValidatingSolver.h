#ifndef KLEE_VALIDATING_SOLVER_H
#define KLEE_VALIDATING_SOLVER_H

#include "klee/Solver.h"
#include "SolverImpl.h"

namespace klee
{
class ValidatingSolver : public SolverImpl
{
private:
  Solver *solver, *oracle;
  void checkIVSolution(
    const Query& query,
    const std::vector<const Array*> &objects,
    std::vector< std::vector<unsigned char> > &values);

public:
  ValidatingSolver(Solver *_solver, Solver *_oracle)
    : solver(_solver), oracle(_oracle) {}
  ~ValidatingSolver() { delete solver; }

  Solver::Validity computeValidity(const Query&);
  bool computeSat(const Query&);
  ref<Expr> computeValue(const Query&);
  bool computeInitialValues(
    const Query&,
    const std::vector<const Array*> &objects,
    std::vector< std::vector<unsigned char> > &values);

  void printName(int level = 0) const {
    klee_message("%*s" "ValidatingSolver containing:", 2*level, "");
    solver->printName(level + 1);
    oracle->printName(level + 1);
  }
protected:
  void failQuery(void);
};
}

#endif

#ifndef KLEE_VALIDATING_SOLVER_H
#define KLEE_VALIDATING_SOLVER_H

#include "klee/Solver.h"
#include "klee/SolverImpl.h"

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

  bool computeValidity(const Query&, Solver::Validity &result);
  bool computeTruth(const Query&, bool &isValid);
  bool computeValue(const Query&, ref<Expr> &result);
  bool computeInitialValues(const Query&,
                            const std::vector<const Array*> &objects,
                            std::vector< std::vector<unsigned char> > &values,
                            bool &hasSolution);

  void printName(int level = 0) const {
    klee_message("%*s" "ValidatingSolver containing:", 2*level, "");
    solver->printName(level + 1);
    oracle->printName(level + 1);
  }
};
}

#endif

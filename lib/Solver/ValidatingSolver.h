#ifndef KLEE_VALIDATING_SOLVER_H
#define KLEE_VALIDATING_SOLVER_H

#include "klee/Solver.h"
#include "SolverImpl.h"

namespace klee
{
class ValidatingSolver : public SolverImpl
{
public:
	ValidatingSolver(Solver *_solver, Solver *_oracle)
	: solver(_solver)
	, oracle(_oracle) {}
	~ValidatingSolver() { delete solver; }

	Solver::Validity computeValidity(const Query&) override;
	bool computeSat(const Query&) override;
	ref<Expr> computeValue(const Query&) override;
	bool computeInitialValues(const Query&, Assignment&) override;

	void printName(int level = 0) const override {
		klee_message("%*s" "ValidatingSolver containing:", 2*level, "");
		solver->printName(level + 1);
		oracle->printName(level + 1);
	}

protected:
	void failQuery(void) override;

private:
	void retryValidity(
		const Query &query,
		Solver::Validity oracleValidity,
		Solver::Validity solverValidity);
	void checkIVSolution(const Query& query, Assignment& a);
	void satMismatch(const Query& q);

	Solver *solver, *oracle;
};
}

#endif

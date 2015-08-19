#ifndef STAGEDSOLVER_H
#define STAGEDSOLVER_H

#include "klee/Solver.h"
#include "SolverImpl.h"

namespace klee
{
/// StagedSolver - Adapter class for staging an incomplete solver with
/// a complete secondary solver, to form an (optimized) complete
/// solver.
class StagedIncompleteSolverImpl : public SolverImpl
{
private:
	std::unique_ptr<IncompleteSolver>	primary;
	std::unique_ptr<Solver>			secondary;

public:
	StagedIncompleteSolverImpl(IncompleteSolver *_primary, Solver *_secondary);
	virtual ~StagedIncompleteSolverImpl() = default;

	bool computeSat(const Query&) override;
	Solver::Validity computeValidity(const Query&) override;
	ref<Expr> computeValue(const Query&) override;
	bool computeInitialValues(const Query&, Assignment&) override;

	void printName(int level = 0) const override {
		klee_message("%*s" "StagedIncompleteSolverImpl containing:", 
			2*level, "");
		primary->printName(level + 1);
		secondary->printName(level + 1);
	}
};


class StagedSolverImpl : public SolverImpl
{
private:
	std::unique_ptr<Solver>	primary;
	std::unique_ptr<Solver>	secondary;
	bool	validityBySat;
protected:
	void failQuery(void) override;
public:
	StagedSolverImpl(
		Solver *_primary,
		Solver *_secondary,
		bool validity_with_sat=false);
	virtual ~StagedSolverImpl() = default;

	bool computeSat(const Query&) override;
	Solver::Validity computeValidity(const Query&) override;
	ref<Expr> computeValue(const Query&) override;
	bool computeInitialValues(const Query&, Assignment&) override;

	void printName(int level = 0) const override {
		klee_message("%*s" "StagedSolverImpl containing:", 2*level, "");
		primary->printName(level + 1);
		secondary->printName(level + 1);
	}
};

}

#endif

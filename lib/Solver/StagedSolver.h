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
	IncompleteSolver	*primary;
	Solver			*secondary;

public:
	StagedIncompleteSolverImpl(IncompleteSolver *_primary, Solver *_secondary);
	virtual ~StagedIncompleteSolverImpl();

	bool computeSat(const Query&);
	Solver::Validity computeValidity(const Query&);
	ref<Expr> computeValue(const Query&);
	bool computeInitialValues(
		const Query&,
		const std::vector<const Array*> &objects,
		std::vector< std::vector<unsigned char> > &values);

	void printName(int level = 0) const {
		klee_message("%*s" "StagedIncompleteSolverImpl containing:", 
			2*level, "");
		primary->printName(level + 1);
		secondary->printName(level + 1);
	}
};


class StagedSolverImpl : public SolverImpl
{
private:
	Solver	*primary;
	Solver	*secondary;
protected:
	virtual void failQuery(void);
public:
	StagedSolverImpl(Solver *_primary, Solver *_secondary);
	virtual ~StagedSolverImpl();

	bool computeSat(const Query&);
	Solver::Validity computeValidity(const Query&);
	ref<Expr> computeValue(const Query&);
	bool computeInitialValues(
		const Query&,
		const std::vector<const Array*> &objects,
		std::vector< std::vector<unsigned char> > &values);

	void printName(int level = 0) const {
		klee_message("%*s" "StagedSolverImpl containing:", 2*level, "");
		primary->printName(level + 1);
		secondary->printName(level + 1);
	}
};

}

#endif

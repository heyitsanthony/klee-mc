/**
 * solver that only works for queries that are already known
 */

#ifndef CONSTSOLVERSOLVER_H
#define CONSTSOLVERSOLVER_H

#include "klee/Solver.h"
#include "SolverImpl.h"

namespace klee
{
#define CONSTSOLVER_FAIL(x)	{ failQuery(); return x; }
#define CONSTSOLVER_SETUP(q)		\
	const ConstantExpr	*ce;	\
	ce = dyn_cast<ConstantExpr>(q.expr);	\
	if (ce == NULL) 


class ConstSolverImpl : public SolverImpl
{
public:
	ConstSolverImpl() {}
	virtual ~ConstSolverImpl() {}

	Solver::Validity computeValidity(const Query& q)
	{
		CONSTSOLVER_SETUP(q) { CONSTSOLVER_FAIL(Solver::Unknown); }

		return (ce->getZExtValue() == 0)
			? Solver::False : Solver::True;
	}

	bool computeSat(const Query& q)
	{ 
		CONSTSOLVER_SETUP(q) { CONSTSOLVER_FAIL(false); }
		return ce->getZExtValue() == 0 ? false : true;
	}
	
	ref<Expr> computeValue(const Query&) {
		CONSTSOLVER_SETUP(q) { CONSTSOLVER_FAIL(NULL); }
		return q.expr; }

	/* XXX: how to handle this if everything is already constant? */
	bool computeInitialValues(const Query&, Assignment& a)
	{ CONSTSOLVER_FAIL(false); }

	void printName(int level = 0) const
	{ klee_message("%*s" "ConstSolverImpl", 2*level, ""); }
};
#undef CONSTSOLVER_FAIL

class ConstSolver : public TimedSolver
{
public:
	ConstSolver(void) : TimedSolver(new ConstSolverImpl()) {}
	virtual ~ConstSolver(void) {}
};

}

#endif

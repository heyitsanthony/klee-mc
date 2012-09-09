#ifndef DUMMYSOLVER_H
#define DUMMYSOLVER_H

#include "klee/Solver.h"
#include "SolverImpl.h"

namespace klee
{
#define DUMMY_FAIL(x)	failQuery(); return x
class DummySolverImpl : public SolverImpl
{
public:
	DummySolverImpl() {}
	virtual ~DummySolverImpl() {}

	Solver::Validity computeValidity(const Query& q)
	{ DUMMY_FAIL(Solver::Unknown); }

	bool computeSat(const Query& q) { DUMMY_FAIL(false); }

	ref<Expr> computeValue(const Query&) { DUMMY_FAIL(NULL); }
	bool computeInitialValues(const Query&, Assignment& a)
	{ DUMMY_FAIL(false); }

	void printName(int level = 0) const
	{ klee_message("%*s" "DummySolverImpl", 2*level, ""); }
};
#undef DUMMY_FAIL

class DummySolver : public TimedSolver
{
public:
	DummySolver(void) : TimedSolver(new DummySolverImpl()) {}
	virtual ~DummySolver(void) {}
};


}

#endif

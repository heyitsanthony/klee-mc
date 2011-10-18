#ifndef TAUTOLOGYCHECKER_H
#define TAUTOLOGYCHECKER_H

#include "klee/Solver.h"
#include "SolverImplWrapper.h"

namespace klee
{

class TautologyChecker : public SolverImplWrapper
{
public:
	TautologyChecker(Solver* s);
	virtual ~TautologyChecker();

	void setTopLevelSolver(Solver* ts) { top_solver = ts; }

	bool computeSat(const Query&);
	Solver::Validity computeValidity(const Query&);
	ref<Expr> computeValue(const Query&);
	bool computeInitialValues(const Query&, Assignment&);

	void printName(int level = 0) const {
		klee_message(
			"%*s TautologyChecker containing: ",
			2*level,  "");
		wrappedSolver->printName(level + 1);
	}

private:
	void checkExpr(const ref<Expr>& e);
	void splitQuery(const Query& q);
	bool	in_solver;	// don't double check!
	Solver	*top_solver;

	unsigned int	split_query_c;
	unsigned int	split_fail_c;
	unsigned int	tautology_c;
};
}
#endif

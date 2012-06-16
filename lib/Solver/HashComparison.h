#ifndef HASHCMPCHECKER_H
#define HASHCMPCHECKER_H

#include "klee/Solver.h"
#include "SolverImplWrapper.h"
#include "QueryHash.h"

namespace klee
{

class HashComparison : public SolverImplWrapper
{
public:
	HashComparison(Solver* s, QueryHash* h1, QueryHash *h2);
	virtual ~HashComparison();

	bool computeSat(const Query&);
	Solver::Validity computeValidity(const Query&);
	ref<Expr> computeValue(const Query&);
	bool computeInitialValues(const Query&, Assignment&);

	void printName(int level = 0) const {
		klee_message(
			"%*s HashComparison containing: ",
			2*level,  "");
		wrappedSolver->printName(level + 1);
	}

private:
	void	dumpQueryHash(const Query& q);
	bool	in_solver;	// don't double check!
	std::set<std::pair<Expr::Hash, Expr::Hash> >	dumped_pairs;
	QueryHash	*qh1, *qh2;
	std::ofstream	*of;
};
}
#endif

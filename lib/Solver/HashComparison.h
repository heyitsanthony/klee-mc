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
	virtual ~HashComparison() = default;

	bool computeSat(const Query&) override;
	Solver::Validity computeValidity(const Query&) override;
	ref<Expr> computeValue(const Query&) override;
	bool computeInitialValues(const Query&, Assignment&) override;

	void printName(int level = 0) const override {
		klee_message(
			"%*s HashComparison containing: ",
			2*level,  "");
		wrappedSolver->printName(level + 1);
	}

private:
	void	dumpQueryHash(const Query& q);
	bool	in_solver;	// don't double check!
	std::set<std::pair<Expr::Hash, Expr::Hash> >	dumped_pairs;
	std::unique_ptr<QueryHash>	qh1, qh2;
	std::unique_ptr<std::ofstream>	of;
};
}
#endif

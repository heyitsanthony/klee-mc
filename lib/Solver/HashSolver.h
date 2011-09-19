#ifndef HASHSOLVER_H
#define HASHSOLVER_H

#include "klee/Solver.h"
#include "QueryHash.h"
#include "SolverImplWrapper.h"

#include <set>

namespace klee
{

class HashSolver : public SolverImplWrapper
{
public:
	HashSolver(Solver* s, QueryHash* phash);
	virtual ~HashSolver();

private:
	unsigned int		hits;
	unsigned int		misses;

	QueryHash		*qhash;
	/* some stupid optimizations so we don't have to recompute 
	 * hashes all the time */
	const Query		*cur_q;
	unsigned		cur_hash;
	bool			q_loaded;

	Assignment* loadCachedAssignment(const std::vector<const Array*>& objs);

	bool getCachedAssignment(
		const Query& q,
		const std::vector<const Array*> &objects,
		std::vector< std::vector<unsigned char> > &values);

	void saveCachedAssignment(
		const std::vector<const Array*> &objects,
		std::vector< std::vector<unsigned char> > &values);

	std::string getHashPath(void) const;

	bool isMatch(Assignment* a) const;

public:
	bool computeSat(const Query&);
	Solver::Validity computeValidity(const Query&);
	bool computeInitialValues(
		const Query&,
		const std::vector<const Array*> &objects,
		std::vector< std::vector<unsigned char> > &values);

	void printName(int level = 0) const {
		klee_message(
			((std::string("%*s HashSolver (")+
				qhash->getName())+
				") containing: ").c_str(),
			2*level,  "");
		wrappedSolver->printName(level + 1);
	}
};
}
#endif

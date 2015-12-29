#ifndef HASHSOLVER_H
#define HASHSOLVER_H

#include "klee/Solver.h"
#include "QueryHash.h"
#include "SolverImplWrapper.h"

#include <string>
#include <vector>
#include <unordered_set>

#include "QHS.h"

namespace klee
{

class HashSolver : public SolverImplWrapper
{
public:
	HashSolver(Solver* s, QueryHash* phash);
	virtual ~HashSolver() = default;

private:
	static unsigned	hits;
	static unsigned	misses;
	static unsigned store_hits;
	
	typedef std::vector< QHSEntry* > missqueue_ty;

	static missqueue_ty	miss_queue;
	std::unique_ptr<QueryHash>	qhash;

	/* some stupid optimizations so we don't have to recompute 
	 * hashes all the time */
	const Query		*cur_q;
	Expr::Hash		cur_hash;
	bool			q_loaded;
	std::unordered_set<Expr::Hash>	sat_hashes;
	std::unordered_set<Expr::Hash>	unsat_hashes;
	std::unordered_set<Expr::Hash>	poison_hashes;
	std::unique_ptr<QHSStore>	qstore;

	Assignment* loadCachedAssignment(const std::vector<const Array*>& objs);

	bool getCachedAssignment(const Query& q, Assignment& a);

	void saveCachedAssignment(const Assignment& a);

	std::string getHashPathSAT(void) const;
	std::string getHashPathUnSAT(void) const;
	std::string getHashPathSolution(void) const;

	bool isMatch(const Assignment& a) const;
	bool computeSatMiss(const Query& q);
	bool isPoisoned(const Query& q);

	bool lookupSAT(const Query& q, bool isSAT);
	void saveSAT(const Query& q, bool isSAT);

	static void touchSAT(
		const Query	&q,
		Expr::Hash	qh,
		bool		isSAT);
	static void touchSAT(const QHSEntry& me);
	ref<Expr> computeValueCached(const Query& q);

public:
	virtual Solver::Validity computeValidity(const Query&);
	ref<Expr> computeValue(const Query& query);

	virtual bool computeSat(const Query&);
	virtual bool computeInitialValues(const Query&, Assignment&);
	
	void printName(int level = 0) const {
		klee_message(
			((std::string("%*s HashSolver (")+
				qhash->getName())+
				") containing: ").c_str(),
			2*level,  "");
		wrappedSolver->printName(level + 1);
	}

	static unsigned getHits(void) { return hits; }
	static unsigned getStoreHits(void) { return hits; }
	static unsigned getMisses(void) { return misses; }

	static bool isSink(void);
	static bool isWriteSAT(void);
};
}
#endif

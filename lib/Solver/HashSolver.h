#ifndef HASHSOLVER_H
#define HASHSOLVER_H

#include "klee/Solver.h"
#include "QueryHash.h"
#include "SolverImplWrapper.h"

#include <string>
#include <vector>
#include <set>

namespace klee
{

class HashSolver : public SolverImplWrapper
{
public:
	HashSolver(Solver* s, QueryHash* phash);
	virtual ~HashSolver();

private:
	static unsigned	hits;
	static unsigned	misses;
	class MissEntry {
	public:
		MissEntry(const Query& _q, Expr::Hash _qh, bool _isSAT)
		: q(_q), qh(_qh), isSAT(_isSAT) {}
		~MissEntry() {}

		Query		q;
		Expr::Hash	qh;
		bool		isSAT;
	};
	
	typedef std::vector< MissEntry* > missqueue_ty;

	static missqueue_ty	miss_queue;
	QueryHash		*qhash;
	/* some stupid optimizations so we don't have to recompute 
	 * hashes all the time */
	const Query		*cur_q;
	Expr::Hash		cur_hash;
	bool			q_loaded;
	std::set<Expr::Hash>	sat_hashes;
	std::set<Expr::Hash>	unsat_hashes;


	Assignment* loadCachedAssignment(const std::vector<const Array*>& objs);

	bool getCachedAssignment(const Query& q, Assignment& a);

	void saveCachedAssignment(const Assignment& a);

	std::string getHashPathSAT(void) const;
	std::string getHashPathUnSAT(void) const;
	std::string getHashPathSolution(void) const;

	bool isMatch(Assignment* a) const;
	bool computeSatMiss(const Query& q);

	bool lookup(const Query& q, bool isSAT);
	void saveSAT(const Query& q, bool isSAT);
	static void writeSAT(
		const Query	&q,
		Expr::Hash	qh,
		bool		isSAT);
	static void writeSAT(const MissEntry& me);

	static void touchSAT(
		const Query	&q,
		Expr::Hash	qh,
		bool		isSAT);
	static void touchSAT(const MissEntry& me);

public:
	virtual Solver::Validity computeValidity(const Query&);
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
	static unsigned getMisses(void) { return misses; }
	static void commitMisses(void);
};
}
#endif

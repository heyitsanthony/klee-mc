#ifndef CACHINGSOLVER_H
#define CACHINGSOLVER_H

#include "klee/Constraints.h"
#include "klee/Solver.h"
#include "klee/SolverStats.h"
#include "SolverImplWrapper.h"
#include "IncompleteSolver.h"
#include <unordered_map>
#include "static/Sugar.h"

namespace klee
{

class CachingSolver : public SolverImplWrapper
{
private:
	ref<Expr> canonicalizeQuery(ref<Expr> originalQuery, bool &negationUsed);
	Solver::Validity computeValidityHit(
		const Query&, IncompleteSolver::PartialValidity);
	void cacheInsert(
		const Query& query, IncompleteSolver::PartialValidity result);

	bool cacheLookup(
		const Query& query, IncompleteSolver::PartialValidity &result);

	struct CacheEntry {
		CacheEntry(const ConstraintManager &c, ref<Expr> q)
		: constraints(c), query(q) {}

		CacheEntry(const CacheEntry &ce)
		: constraints(ce.constraints), query(ce.query) {}

		ConstraintManager constraints;
		ref<Expr> query;

		bool operator==(const CacheEntry &b) const
		{
			return	constraints==b.constraints && 
				*query.get()==*b.query.get();
		}
	};
  
	struct CacheEntryHash
	{
	unsigned operator()(const CacheEntry &ce) const
	{
		unsigned result = ce.query->hash();

		foreach (it, ce.constraints.begin(), ce.constraints.end())
			result ^= (*it)->hash();

		return result;
	}
	};

	typedef std::unordered_map<
		CacheEntry, 
		IncompleteSolver::PartialValidity, 
		CacheEntryHash> cache_map;
  
	cache_map cache;
public:
	CachingSolver(Solver *s) : SolverImplWrapper(s) {}
	virtual ~CachingSolver() { cache.clear(); }

	Solver::Validity computeValidity(const Query&);
	bool computeSat(const Query&);

	ref<Expr> computeValue(const Query& query)
	{ return doComputeValue(query); }

	bool computeInitialValues(const Query& query, Assignment& a)
	{ return doComputeInitialValues(query, a); }

	void printName(int level = 0) const {
		klee_message("%*s" "CachingSolver containing:", 2*level, "");
		wrappedSolver->printName(level + 1);
	}

	static uint64_t getHits(void) { return stats::queryCacheHits; }
	static uint64_t getMisses(void) { return stats::queryCacheMisses; }
};

}
#endif

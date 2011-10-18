//===-- CachingSolver.cpp - Caching expression solver ---------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#include "klee/Solver.h"
#include "klee/Constraints.h"
#include "klee/Expr.h"
#include "SolverImplWrapper.h"
#include "klee/SolverStats.h"

#include "IncompleteSolver.h"
#include "static/Sugar.h"
#include <tr1/unordered_map>

using namespace klee;

class CachingSolver : public SolverImplWrapper
{
private:
  ref<Expr> canonicalizeQuery(ref<Expr> originalQuery, bool &negationUsed);
  Solver::Validity computeValidityHit(
  	const Query&, IncompleteSolver::PartialValidity);
  void cacheInsert(const Query& query,
                   IncompleteSolver::PartialValidity result);

  bool cacheLookup(const Query& query,
                   IncompleteSolver::PartialValidity &result);
  
  struct CacheEntry {
    CacheEntry(const ConstraintManager &c, ref<Expr> q)
      : constraints(c), query(q) {}

    CacheEntry(const CacheEntry &ce)
      : constraints(ce.constraints), query(ce.query) {}
    
    ConstraintManager constraints;
    ref<Expr> query;

    bool operator==(const CacheEntry &b) const {
      return constraints==b.constraints && *query.get()==*b.query.get();
    }
  };
  
  struct CacheEntryHash {
    unsigned operator()(const CacheEntry &ce) const {
      unsigned result = ce.query->hash();
      
      foreach (it, ce.constraints.begin(), ce.constraints.end())
        result ^= (*it)->hash();
      
      return result;
    }
  };

  typedef std::tr1::unordered_map<CacheEntry, 
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
};

/** @returns the canonical version of the given query.  The reference
    negationUsed is set to true if the original query was negated in
    the canonicalization process. */
ref<Expr> CachingSolver::canonicalizeQuery(
  ref<Expr> originalQuery,
  bool &negationUsed)
{
  ref<Expr> negatedQuery = Expr::createIsZero(originalQuery);

  // select the "smaller" query to the be canonical representation
  if (originalQuery.compare(negatedQuery) < 0) {
    negationUsed = false;
    return originalQuery;
  } else {
    negationUsed = true;
    return negatedQuery;
  }
}

/** @returns true on a cache hit, false of a cache miss.  Reference
    value result only valid on a cache hit. */
bool CachingSolver::cacheLookup(
	const Query& query,
	IncompleteSolver::PartialValidity &result)
{
  bool negationUsed;
  ref<Expr> canonicalQuery = canonicalizeQuery(query.expr, negationUsed);

  CacheEntry ce(query.constraints, canonicalQuery);
  cache_map::iterator it = cache.find(ce);
  
  if (it != cache.end()) {
    result = (negationUsed ?
              IncompleteSolver::negatePartialValidity(it->second) :
              it->second);
    return true;
  }
  
  return false;
}

/// Inserts the given query, result pair into the cache.
void CachingSolver::cacheInsert(const Query& query,
                                IncompleteSolver::PartialValidity result) {
  bool negationUsed;
  ref<Expr> canonicalQuery = canonicalizeQuery(query.expr, negationUsed);

  CacheEntry ce(query.constraints, canonicalQuery);
  IncompleteSolver::PartialValidity cachedResult = 
    (negationUsed ? IncompleteSolver::negatePartialValidity(result) : result);
  
  cache.insert(std::make_pair(ce, cachedResult));
}


Solver::Validity CachingSolver::computeValidityHit(
	const Query& query,
	IncompleteSolver::PartialValidity cachedResult)
{
	bool	isSat;

	++stats::queryCacheHits;

	switch(cachedResult) {
	case IncompleteSolver::MustBeTrue: return Solver::True;
	case IncompleteSolver::MustBeFalse: return Solver::False;
	case IncompleteSolver::TrueOrFalse:  return Solver::Unknown;
	case IncompleteSolver::MayBeTrue:
		isSat = doComputeSat(query.negateExpr());
		if (failed()) break;

		if (!isSat) {
			cacheInsert(query, IncompleteSolver::MustBeTrue);
			return Solver::True;
		}

		cacheInsert(query, IncompleteSolver::TrueOrFalse);
		return Solver::Unknown;
	case IncompleteSolver::MayBeFalse:
		isSat = doComputeSat(query);
		if (failed()) break;
		if (!isSat) {
			cacheInsert(query, IncompleteSolver::MustBeFalse);
			return Solver::False;
		}
		cacheInsert(query, IncompleteSolver::TrueOrFalse);
		return Solver::Unknown;
	default: assert(0 && "unreachable");
	}

	return Solver::Unknown;
}

Solver::Validity CachingSolver::computeValidity(const Query& query)
{
	IncompleteSolver::PartialValidity cachedResult;
	Solver::Validity	v;
	bool			cacheHit;

	cacheHit = cacheLookup(query, cachedResult);
	if (cacheHit) return computeValidityHit(query, cachedResult);

	++stats::queryCacheMisses;

	v = doComputeValidity(query);
	if (failed()) return v;

	switch (v) {
	case Solver::True: cachedResult = IncompleteSolver::MustBeTrue; break;
	case Solver::False: cachedResult = IncompleteSolver::MustBeFalse; break;
	default: cachedResult = IncompleteSolver::TrueOrFalse; break;
	}

	cacheInsert(query, cachedResult);
	return v;
}

bool CachingSolver::computeSat(const Query& query)
{
	IncompleteSolver::PartialValidity cachedResult;
	bool cacheHit = cacheLookup(query, cachedResult);
	bool isSat;

	if (cacheHit) {
		++stats::queryCacheHits;
		if (cachedResult == IncompleteSolver::MustBeFalse)
			return false;
		if (	cachedResult == IncompleteSolver::MustBeTrue ||
			cachedResult == IncompleteSolver::MayBeTrue ||
			cachedResult == IncompleteSolver::TrueOrFalse)
		{
			return true;
		}

		/* {None, MayBeFalse} */
	}

	++stats::queryCacheMisses;
  
	// cache miss: query solver
	isSat = doComputeSat(query);
	if (failed()) return isSat;

	if (isSat) {
		if (cacheHit) {
			if (cachedResult == IncompleteSolver::MayBeFalse) {
				cachedResult = IncompleteSolver::TrueOrFalse;
			} else {
				assert (cachedResult == IncompleteSolver::None);
				cachedResult = IncompleteSolver::MayBeTrue;
			}
		} else {
			cachedResult = IncompleteSolver::MayBeTrue;
		}
		cacheInsert(query, IncompleteSolver::MayBeTrue);
	} else {
		if (cacheHit && cachedResult == IncompleteSolver::MayBeFalse) {
			cacheInsert(query, IncompleteSolver::MustBeFalse);
		}
	}

	return isSat;
}

///
Solver *klee::createCachingSolver(Solver *_solver) {
  return new Solver(new CachingSolver(_solver));
}

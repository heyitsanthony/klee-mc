//===-- CachingSolver.cpp - Caching expression solver ---------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#include "klee/Expr.h"
#include "klee/SolverStats.h"

#include "CachingSolver.h"

using namespace klee;

unsigned g_cachingsolver_sz = 0;


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
	}

	negationUsed = true;
	return negatedQuery;
}

/** @returns true on a cache hit, false of a cache miss.  Reference
    value result only valid on a cache hit. */
bool CachingSolver::cacheLookup(
	const Query& query,
	IncompleteSolver::PartialValidity &result)
{
	bool		negationUsed;
	ref<Expr>	canonicalQuery;
	cache_map::iterator it;
	
	canonicalQuery = canonicalizeQuery(query.expr, negationUsed);

	CacheEntry ce(query.constraints, canonicalQuery);
	it = cache.find(ce);

	if (it == cache.end()) {
		return false;
	}

	result = (negationUsed)
		? IncompleteSolver::negatePartialValidity(it->second)
		: it->second;
	return true;
}

#define CACHE_WATERMARK	2000

/// Inserts the given query, result pair into the cache.
void CachingSolver::cacheInsert(
	const Query& query,
	IncompleteSolver::PartialValidity result)
{
  bool negationUsed;
  ref<Expr> canonicalQuery = canonicalizeQuery(query.expr, negationUsed);

  CacheEntry ce(query.constraints, canonicalQuery);
  IncompleteSolver::PartialValidity cachedResult = 
    (negationUsed ? IncompleteSolver::negatePartialValidity(result) : result);
  
  if (cache.size() > CACHE_WATERMARK)
    cache.clear();
  cache.insert(std::make_pair(ce, cachedResult));
  g_cachingsolver_sz = cache.size();
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
	bool	cacheHit, isSat;

	cacheHit = cacheLookup(query, cachedResult);
	if (cacheHit) {
		if (cachedResult == IncompleteSolver::MustBeFalse) {
			++stats::queryCacheHits;
			return false;
		}

		if (	cachedResult == IncompleteSolver::MustBeTrue ||
			cachedResult == IncompleteSolver::MayBeTrue ||
			cachedResult == IncompleteSolver::TrueOrFalse)
		{
			++stats::queryCacheHits;
			return true;
		}

		/* {None, MayBeFalse} */
	}

	++stats::queryCacheMisses;
  
	// cache miss: query solver
	isSat = doComputeSat(query);
	if (failed()) return isSat;

 	if (isSat == false) {
		if (cacheHit && cachedResult == IncompleteSolver::MayBeFalse) {
			cacheInsert(query, IncompleteSolver::MustBeFalse);
		}

		return false;
	}

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
	cacheInsert(query, cachedResult);

	return true;
}

Solver *klee::createCachingSolver(Solver *_solver)
{ return new Solver(new CachingSolver(_solver)); }

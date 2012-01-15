//===-- CexCachingSolver.cpp ----------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "klee/Solver.h"
#include "static/Sugar.h"
#include "klee/Constraints.h"
#include "klee/Expr.h"
#include "SolverImplWrapper.h"
#include "klee/SolverStats.h"
#include "klee/TimerStatIncrementer.h"
#include "klee/util/Assignment.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/ExprVisitor.h"
#include "klee/Internal/ADT/MapOfSets.h"
#include "klee/Common.h"

#include "llvm/Support/CommandLine.h"
#include "SMTPrinter.h"

#define MAX_BINDING_BYTES	(32*1024)
#define MAX_CACHED_BYTES	(128*1024*1024)	/* custom alloc => hugetlb */


using namespace klee;
using namespace llvm;

unsigned g_cexcache_sz = 0;

namespace {
	cl::opt<bool>
	DebugCexCacheCheckBinding("debug-cex-cache-check-binding");

	cl::opt<bool>
	CexCacheTryAll(
		"cex-cache-try-all",
		 cl::desc("try substituting all counterexamples before asking STP"),
		 cl::init(false));

	cl::opt<bool>
	CexCacheExperimental("cex-cache-exp", cl::init(false));
}

typedef std::set< ref<Expr> > Key;

struct AssignmentLessThan {
bool operator()(const Assignment *a, const Assignment *b) {
	return a->bindings < b->bindings;
}
};


class CexCachingSolver : public SolverImplWrapper
{
public:
	CexCachingSolver(Solver *_solver)
	: SolverImplWrapper(_solver)
	, assignTab_bytes(0)
	, evicted_bytes(0) {}
	virtual ~CexCachingSolver();

	bool computeSat(const Query&);
	Solver::Validity computeValidity(const Query&);
	ref<Expr> computeValue(const Query&);
	bool computeInitialValues(const Query&, Assignment& a);

	void printName(int level = 0) const
	{
		klee_message("%*s" "CexCachingSolver containing:", 2*level, "");
		wrappedSolver->printName(level + 1);
	}

private:
	Assignment* createBinding(const Query& query, Key& key);

	void evictRandom(void);
	bool searchForAssignment(Key &key, Assignment *&result);
	bool lookupAssignment(
		const Query& query, Key &key, Assignment *&result);
	bool lookupAssignment(const Query& query, Assignment *&result)
	{
		Key key;
		return lookupAssignment(query, key, result);
	}

	bool getAssignment(const Query& query, Assignment *&result);
	Assignment* addToTable(Assignment* binding);

	MapOfSets<ref<Expr>, Assignment*>	cache;

	typedef std::set<Assignment*, AssignmentLessThan> assignTab_ty;
	assignTab_ty				assignTab; // memo table
	unsigned int				assignTab_bytes;
	unsigned int				evicted_bytes;
};

struct NullAssignment { bool operator()(Assignment *a) const { return !a; } };

struct NonNullAssignment {
  bool operator()(Assignment *a) const { return a!=0; }
};

struct NullOrSatisfyingAssignment {
  Key &key;

  NullOrSatisfyingAssignment(Key &_key) : key(_key) {}

  bool operator()(Assignment *a) const {
    return !a || a->satisfies(key.begin(), key.end());
  }
};

/// searchForAssignment - Look for a cached solution for a query.
///
/// \param key - The query to look up.
/// \param result [out] - The cached result, if the lookup is succesful. This is
/// either a satisfying assignment (for a satisfiable query), or 0 (for an
/// unsatisfiable query).
/// \return - True if a cached result was found.
bool CexCachingSolver::searchForAssignment(Key &key, Assignment *&result)
{
	Assignment * const *lookup = cache.lookup(key);
	Assignment **lookup_super;

	if (lookup) {
		result = *lookup;
		return true;
	}

	// Look for a satisfying assignment for a superset, which is trivially an
	// assignment for any subset.
	lookup_super = cache.findSuperset(key, NonNullAssignment());

	// Otherwise, look for a subset which is unsatisfiable -- if the subset is
	// unsatisfiable then no additional constraints can produce a valid
	// assignment. While searching subsets, we also explicitly the solutions for
	// satisfiable subsets to see if they solve the current query and return
	// them if so. This is cheap and frequently succeeds.
	if (!lookup_super) {
		if (CexCacheTryAll)  {
			lookup_super = cache.findSubset(key, NullAssignment());
		} else {
			lookup_super = cache.findSubset(
				key, NullOrSatisfyingAssignment(key));
		}
	}

	// If either lookup succeeded, then we have a cached solution.
	if (lookup_super) {
		result = *lookup_super;
		return true;
	}

	if (CexCacheTryAll) {
		// Otherwise, iterate through the set of current assignments 
		// to see if one of them satisfies the query.
		foreach (it, assignTab.begin(), assignTab.end()) {
			Assignment *a = *it;
			if (a->satisfies(key.begin(), key.end())) {
				// cache result for deterministic reconstitution
				result = a;
				cache.insert(key, result);
				return true;
			}
		}
	}

	return false;
}

/// lookupAssignment - Lookup a cached result for the given \arg query.
///
/// \param query - The query to lookup.
/// \param key [out] - On return, the key constructed for the query.
/// \param result [out] - The cached result, if the lookup is succesful. This is
/// either a satisfying assignment (for a satisfiable query), or 0 (for an
/// unsatisfiable query).
/// \return True if a cached result was found.
bool CexCachingSolver::lookupAssignment(
	const Query &query, Key &key, Assignment *&result)
{
	ref<Expr> neg;

	key = Key(query.constraints.begin(), query.constraints.end());
	neg = Expr::createIsZero(query.expr);
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(neg)) {
		if (CE->isFalse()) {
			result = NULL;
			return true;
		}
	} else {
		key.insert(neg);
	}

	return searchForAssignment(key, result);
}

void CexCachingSolver::evictRandom(void)
{
	std::set<Assignment*>				as_to_del;
	std::vector<std::pair<Key, Assignment*> >	expr_sets;

	/* collect assignments to trash */
	foreach (it, assignTab.begin(), assignTab.end()) {
		/* 50% chance of being thrown in bit-bin */
		if (rand() % 2)
			as_to_del.insert(*it);
	}

	/* save non-matching references from ref=>Assignment cache */
	foreach (it, cache.begin(), cache.end()) {
		Assignment	*cur_a = (*it).second;

		if (!as_to_del.count(cur_a))
			expr_sets.push_back(*it);
	}

	/* clear, rebuild */
	cache.clear();
	foreach (it, expr_sets.begin(), expr_sets.end()) {
		cache.insert((*it).first, (*it).second);
	}
	expr_sets.clear();

	/* delete all */
	foreach (it, as_to_del.begin(), as_to_del.end()) {
		Assignment	*a(*it);
		unsigned int	a_bytes;

		a_bytes = a->getBindingBytes();
		assignTab_bytes -= a_bytes;
		evicted_bytes += a_bytes;

		assignTab.erase(a);
		delete a;
	}

	std::cerr
		<< "[CexCache] Total Evicted Bytes: "
		<< evicted_bytes << '\n';
}

Assignment* CexCachingSolver::addToTable(Assignment* binding)
{
	std::pair<assignTab_ty::iterator, bool>	res;
	unsigned int	new_bytes = 0;

	if ((	assignTab_bytes + MAX_BINDING_BYTES) > MAX_CACHED_BYTES ||
		assignTab.size() > 2000)
	{
		new_bytes = binding->getBindingBytes();
		evictRandom();
	}

	res = assignTab.insert(binding);
	if (res.second == true) {
		if (!new_bytes) new_bytes = binding->getBindingBytes();
		assignTab_bytes += new_bytes;
		return binding;
	}

	/* binding already exists */
	assert (binding != *res.first);
	delete binding;

	g_cexcache_sz = assignTab.size();
	return *res.first;
}

Assignment* CexCachingSolver::createBinding(const Query& query, Key& key)
{
	std::vector<const Array*>	objects;
	Assignment			*binding;
	bool				hasSolution;

	findSymbolicObjects(key.begin(), key.end(), objects);
	binding = new Assignment(objects);

	hasSolution = doComputeInitialValues(query, *binding);
	if (failed() || !hasSolution) {
		delete binding;
		return NULL;
	}

	// Memoize the result.
	binding = addToTable(binding);

	if (DebugCexCacheCheckBinding) {
		assert (binding->satisfies(key.begin(), key.end()));
	}

	return binding;
}

bool CexCachingSolver::getAssignment(const Query& query, Assignment* &result)
{
	Key key;

	if (lookupAssignment(query, key, result)) {
		return true;
	}

	result = createBinding(query, key);
	if (failed()) return false;
	if (result == NULL) return true;

	cache.insert(key, result);

	return true;
}

CexCachingSolver::~CexCachingSolver()
{
	cache.clear();
	foreach (it, assignTab.begin(),assignTab.end())
		delete *it;
}

Solver::Validity CexCachingSolver::computeValidity(const Query& query)
{
	TimerStatIncrementer t(stats::cexCacheTime);
	bool		ok_assignment;
	ref<Expr>	neg_q;
	Assignment	*neg_a = NULL, *pos_a = NULL;

	ok_assignment = getAssignment(query.withFalse(), neg_a);
	if (!ok_assignment) goto failed;

	assert(neg_a && "computeValidity() must have assignment");
	neg_q = neg_a->evaluate(query.expr);

	if (!isa<ConstantExpr>(neg_q)) {
		if (!getAssignment(query, pos_a)) goto failed;
		if (pos_a) {
			if (!getAssignment(query.negateExpr(), neg_a))
				goto failed;

			return (!neg_a ? Solver::False : Solver::Unknown);
		}
		return Solver::True;
	}

	if (cast<ConstantExpr>(neg_q)->isTrue()) {
		if (!getAssignment(query, pos_a)) goto failed;
		return !pos_a ? Solver::True : Solver::Unknown;
	}

	if (!getAssignment(query.negateExpr(), neg_a)) goto failed;

	return !neg_a ? Solver::False : Solver::Unknown;

failed:
	failQuery();
	return Solver::Unknown;
}

bool CexCachingSolver::computeSat(const Query& query)
{
	TimerStatIncrementer t(stats::cexCacheTime);
	Assignment *cex;

	// There is a small amount of redundancy here. We only need to know
	// truth and do not really need to compute an assignment. This means
	// that we could check the cache to see if we already know that
	// state ^ query has no assignment. In that case, by the validity of
	// state, we know that state ^ !query must have an assignment, and
	// so query cannot be true (valid). This does get hits, but doesn't
	// really seem to be worth the overhead.

	if (CexCacheExperimental) {
		assert (0 == 1 && "BROKEN");
		if (lookupAssignment(query.negateExpr(), cex) && !cex)
			goto failed;
	}

	if (!getAssignment(query.negateExpr(), cex)) goto failed;

	/* counter example to negation => satisfiable */
	return (cex != NULL);

failed:
	failQuery();
	return false;
}

ref<Expr> CexCachingSolver::computeValue(const Query& query)
{
	TimerStatIncrementer	t(stats::cexCacheTime);
	Assignment		*a;
	ref<Expr>		ret;
	bool			zeroDiv;

	if (!getAssignment(query.withFalse(), a)) {
		failQuery();
		return ret;
	}

	assert(a && "computeValue() must have assignment");
	ret = a->evaluate(query.expr, zeroDiv);

	if (!isa<ConstantExpr>(ret) ) {
		if (zeroDiv) {
			klee_warning_once(0,
				"Div zero in CexCache. Need richer failures");
			failQuery();
			return ret;
		}
	}

	assert(	isa<ConstantExpr>(ret) &&
		"assignment evaluation did not result in constant");

	return ret;
}

bool CexCachingSolver::computeInitialValues(
	const Query& query,
	Assignment& a_out)
{
	TimerStatIncrementer	t(stats::cexCacheTime);
	Assignment		*a;
	bool			hasSolution;
	unsigned int		a_bytes;

	a_bytes = a_out.getBindingBytes();
	if (a_bytes > MAX_BINDING_BYTES) {
		/* don't cache large queries */
		return doComputeInitialValues(query, a_out);
	}

	a = NULL;
	if (getAssignment(query, a) == false) {
		failQuery();
		return false;
	}

	hasSolution = (a != NULL);
	if (a == NULL) {
		return hasSolution;
	}

	// FIXME: We should use smarter assignment for result so we don't
	// need redundant copy.
	foreach (it, a->bindingsBegin(), a->bindingsEnd())
		a_out.bindFree(it->first, it->second);

	/* zero out parts with no assignment */
	a_out.bindFreeToZero();

	return hasSolution;
}

Solver *klee::createCexCachingSolver(Solver *_solver)
{ return new Solver(new CexCachingSolver(_solver)); }

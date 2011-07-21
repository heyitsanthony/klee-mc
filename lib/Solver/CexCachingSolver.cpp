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

#include "llvm/Support/CommandLine.h"

using namespace klee;
using namespace llvm;

namespace {
  cl::opt<bool>
  DebugCexCacheCheckBinding("debug-cex-cache-check-binding");

  cl::opt<bool>
  CexCacheTryAll("cex-cache-try-all",
                 cl::desc("try substituting all counterexamples before asking STP"),
                 cl::init(false));

  cl::opt<bool>
  CexCacheExperimental("cex-cache-exp", cl::init(false));

}

///

typedef std::set< ref<Expr> > KeyType;

struct AssignmentLessThan {
  bool operator()(const Assignment *a, const Assignment *b) {
    return a->bindings < b->bindings;
  }
};


class CexCachingSolver : public SolverImplWrapper
{
  typedef std::set<Assignment*, AssignmentLessThan> assignmentsTable_ty;

  MapOfSets<ref<Expr>, Assignment*> cache;
  // memo table
  assignmentsTable_ty assignmentsTable;

  bool searchForAssignment(KeyType &key,
                           Assignment *&result);

  bool lookupAssignment(const Query& query, KeyType &key, Assignment *&result);

  bool lookupAssignment(const Query& query, Assignment *&result) {
    KeyType key;
    return lookupAssignment(query, key, result);
  }

  bool getAssignment(const Query& query, Assignment *&result);

public:
  CexCachingSolver(Solver *_solver) : SolverImplWrapper(_solver) {}
  virtual ~CexCachingSolver();

  bool computeSat(const Query&);
  Solver::Validity computeValidity(const Query&);
  ref<Expr> computeValue(const Query&);
  bool computeInitialValues(
  	const Query&,
	const std::vector<const Array*> &objects,
	std::vector< std::vector<unsigned char> > &values);

  void printName(int level = 0) const {
    klee_message("%*s" "CexCachingSolver containing:", 2*level, "");
    wrappedSolver->printName(level + 1);
  }
};

///

struct NullAssignment {
  bool operator()(Assignment *a) const { return !a; }
};

struct NonNullAssignment {
  bool operator()(Assignment *a) const { return a!=0; }
};

struct NullOrSatisfyingAssignment {
  KeyType &key;

  NullOrSatisfyingAssignment(KeyType &_key) : key(_key) {}

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
bool CexCachingSolver::searchForAssignment(KeyType &key, Assignment *&result)
{
  Assignment * const *lookup = cache.lookup(key);
  if (lookup) {
    result = *lookup;
    return true;
  }

  if (CexCacheTryAll) {
    // Look for a satisfying assignment for a superset, which is trivially an
    // assignment for any subset.
    Assignment **lookup = cache.findSuperset(key, NonNullAssignment());

    // Otherwise, look for a subset which is unsatisfiable, see below.
    if (!lookup)
      lookup = cache.findSubset(key, NullAssignment());

    // If either lookup succeeded, then we have a cached solution.
    if (lookup) {
      result = *lookup;
      return true;
    }

    // Otherwise, iterate through the set of current assignments to see if one
    // of them satisfies the query.
    foreach (it, assignmentsTable.begin(), assignmentsTable.end()) {
      Assignment *a = *it;
      if (a->satisfies(key.begin(), key.end())) {
        result = a;
        // cache result for deterministic reconstitution
        cache.insert(key, result);
        return true;
      }
    }
  } else {
    // FIXME: Which order? one is sure to be better.

    // Look for a satisfying assignment for a superset, which is trivially an
    // assignment for any subset.
    Assignment **lookup = cache.findSuperset(key, NonNullAssignment());

    // Otherwise, look for a subset which is unsatisfiable -- if the subset is
    // unsatisfiable then no additional constraints can produce a valid
    // assignment. While searching subsets, we also explicitly the solutions for
    // satisfiable subsets to see if they solve the current query and return
    // them if so. This is cheap and frequently succeeds.
    if (!lookup)
      lookup = cache.findSubset(key, NullOrSatisfyingAssignment(key));

    // If either lookup succeeded, then we have a cached solution.
    if (lookup) {
      result = *lookup;
      // cache result for deterministic reconstitution
      cache.insert(key, result);
      return true;
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
  const Query &query, KeyType &key, Assignment *&result)
{
  key = KeyType(query.constraints.begin(), query.constraints.end());
  ref<Expr> neg = Expr::createIsZero(query.expr);
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(neg)) {
    if (CE->isFalse()) {
      result = (Assignment*) 0;
      return true;
    }
  } else {
    key.insert(neg);
  }

  return searchForAssignment(key, result);
}

bool CexCachingSolver::getAssignment(const Query& query, Assignment *&result)
{
  KeyType key;
  if (lookupAssignment(query, key, result))
    return true;

  std::vector<const Array*> objects;
  findSymbolicObjects(key.begin(), key.end(), objects);

  std::vector< std::vector<unsigned char> > values;
  bool hasSolution;
  hasSolution = doComputeInitialValues(query, objects, values);
  if (failed()) return false;

  Assignment *binding;
  if (hasSolution) {
    binding = new Assignment(objects, values);

    // Memoize the result.
    std::pair<assignmentsTable_ty::iterator, bool>
      res = assignmentsTable.insert(binding);
    if (!res.second) {
      delete binding;
      binding = *res.first;
    }

    if (DebugCexCacheCheckBinding)
      assert(binding->satisfies(key.begin(), key.end()));
  } else {
    binding = (Assignment*) 0;
  }

  result = binding;
  cache.insert(key, binding);

  return true;
}

CexCachingSolver::~CexCachingSolver()
{
	cache.clear();
	foreach (it, assignmentsTable.begin(),assignmentsTable.end())
		delete *it;
}

Solver::Validity CexCachingSolver::computeValidity(const Query& query)
{
	TimerStatIncrementer t(stats::cexCacheTime);
	bool		ok_assignment;
	ref<Expr>	q;
	Assignment	*a;

	ok_assignment = getAssignment(query.withFalse(), a);
	if (!ok_assignment) goto failed;

	assert(a && "computeValidity() must have assignment");
	q = a->evaluate(query.expr);

	if (!isa<ConstantExpr>(q)) {
		if (!getAssignment(query, a)) goto failed;
		if (a) {
			if (!getAssignment(query.negateExpr(), a)) goto failed;
			return (!a ? Solver::False : Solver::Unknown);
		}
		return Solver::True;
	} else if (cast<ConstantExpr>(q)->isTrue()) {
		if (!getAssignment(query, a)) goto failed;
		return !a ? Solver::True : Solver::Unknown;
	} else {
		if (!getAssignment(query.negateExpr(), a)) goto failed;
		return !a ? Solver::False : Solver::Unknown;
	}

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

	if (!getAssignment(query.withFalse(), a)) {
		failQuery();
		return ret;
	}

	assert(a && "computeValue() must have assignment");
	ret = a->evaluate(query.expr);
	assert(	isa<ConstantExpr>(ret) &&
		"assignment evaluation did not result in constant");

	return ret;
}

bool CexCachingSolver::computeInitialValues(
	const Query& query,
	const std::vector<const Array*> &objects,
	std::vector< std::vector<unsigned char> > &values)
{
	TimerStatIncrementer	t(stats::cexCacheTime);
	Assignment		*a;
	bool			hasSolution;

	if (!getAssignment(query, a)) {
		failQuery();
		return false;
	}

	hasSolution = !!a;

	if (!a) return hasSolution;

	// FIXME: We should use smarter assignment for result so we don't
	// need redundant copy.
	values = std::vector< std::vector<unsigned char> >(objects.size());
	for (unsigned i=0; i < objects.size(); ++i) {
		const Array *arr = objects[i];
		Assignment::bindings_ty::iterator it = a->bindings.find(arr);
		if (it == a->bindings.end()) {
			values[i] = std::vector<unsigned char>(
				arr->mallocKey.size, 0);
		} else {
			values[i] = it->second;
		}
	}

	return hasSolution;
}

///

Solver *klee::createCexCachingSolver(Solver *_solver) {
  return new Solver(new CexCachingSolver(_solver));
}

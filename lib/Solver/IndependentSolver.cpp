//===-- IndependentSolver.cpp ---------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Solver.h"

#include "klee/Expr.h"
#include "klee/Constraints.h"
#include "SolverImplWrapper.h"

#include "klee/util/ExprUtil.h"

#include "static/Sugar.h"

#include <map>
#include <vector>
#include <ostream>
#include <iostream>

using namespace klee;
using namespace llvm;

template<class T>
class DenseSet {
  typedef std::set<T> set_ty;
  set_ty s;

public:
  DenseSet() {}

  void add(T x) { s.insert(x); }
  void add(T start, T end) { for (; start<end; start++) s.insert(start); }

  // returns true iff set is changed by addition
  bool add(const DenseSet &b) {
    bool modified = false;
    foreach (it, b.s.begin(), b.s.end()) {
      if (modified || !s.count(*it)) {
        modified = true;
        s.insert(*it);
      }
    }
    return modified;
  }

  bool intersects(const DenseSet &b) {
    foreach (it, s.begin(), s.end()) {
      if (b.s.count(*it))
        return true;
    }
    return false;
  }

  void print(std::ostream &os) const {
    bool first = true;
    os << "{";
    foreach (it, s.begin(), s.end()) {
      if (first) {
        first = false;
      } else {
        os << ",";
      }
      os << *it;
    }
    os << "}";
  }
};

template<class T>
inline std::ostream &operator<<(std::ostream &os, const DenseSet<T> &dis)
{ dis.print(os); return os; }

class IndependentElementSet
{
	typedef std::map<const Array*, DenseSet<unsigned> > elements_ty;
	elements_ty			elements;
	std::set<const Array*>		wholeObjects;

public:
	IndependentElementSet() {}
	IndependentElementSet(ref<Expr> e);

	IndependentElementSet(const IndependentElementSet &ies)
	: elements(ies.elements)
	, wholeObjects(ies.wholeObjects) {}

	IndependentElementSet &operator=(const IndependentElementSet &ies)
	{
		elements = ies.elements;
		wholeObjects = ies.wholeObjects;
		return *this;
	}

  void print(std::ostream &os) const {
    os << "{";
    bool first = true;
    foreach (it, wholeObjects.begin(), wholeObjects.end()) {
      const Array *array = *it;

      if (first) {
        first = false;
      } else {
        os << ", ";
      }

      os << "MO" << array->name;
    }
    foreach (it, elements.begin(), elements.end()) {
      const Array *array = it->first;
      const DenseSet<unsigned> &dis = it->second;

      if (first) {
        first = false;
      } else {
        os << ", ";
      }

      os << "MO" << array->name << " : " << dis;
    }
    os << "}";
  }

	bool intersects(const IndependentElementSet &b); 

	// returns true iff set is changed by addition
	bool add(const IndependentElementSet &b);
};

// more efficient when this is the smaller set
bool IndependentElementSet::intersects(const IndependentElementSet &b)
{
	foreach (it, wholeObjects.begin(), wholeObjects.end()) {
		const Array *array = *it;
		if (	b.wholeObjects.count(array) ||
			b.elements.find(array) != b.elements.end())
			return true;
	}

	foreach (it, elements.begin(), elements.end()) {
		const Array *array = it->first;
		elements_ty::const_iterator it2;

		if (b.wholeObjects.count(array))
			return true;
		it2 = b.elements.find(array);
		if (it2 != b.elements.end()) {
			if (it->second.intersects(it2->second))
				return true;
		}
	}

	return false;
}

bool IndependentElementSet::add(const IndependentElementSet &b)
{
	bool	modified = false;

	foreach (it, b.wholeObjects.begin(), b.wholeObjects.end()) {
		const Array *array;
		elements_ty::iterator it2;

		it2 = elements.find(array);
		if (it2 != elements.end()) {
			modified = true;
			elements.erase(it2);
			wholeObjects.insert(array);
			continue;
		}

		array = *it;
		if (!wholeObjects.count(array)) {
			modified = true;
			wholeObjects.insert(array);
		}
	}

	foreach (it, b.elements.begin(), b.elements.end()) {
		const Array *array = it->first;
		elements_ty::iterator it2;

		if (wholeObjects.count(array))
			continue;

		it2 = elements.find(array);
		if (it2==elements.end()) {
			modified = true;
			elements.insert(*it);
			continue;
		}

		if (it2->second.add(it->second))
			modified = true;
	}

	return modified;
}


IndependentElementSet::IndependentElementSet(ref<Expr> e)
{
	std::vector< ref<ReadExpr> > reads;

	ExprUtil::findReads(e, /* visitUpdates= */ true, reads);
	for (unsigned i = 0; i != reads.size(); ++i) {
		ReadExpr *re = reads[i].get();
		const Array *array = re->updates.getRoot().get();

		// Reads of a constant array don't alias.
		if (	re->updates.getRoot().get()->isConstantArray() &&
			re->updates.head == NULL)
		{
			continue;
		}

		if (wholeObjects.count(array))
			continue;

		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(re->index)) {
			DenseSet<unsigned> &dis = elements[array];
			dis.add((unsigned) CE->getZExtValue(32));
		} else {
			elements_ty::iterator it2 = elements.find(array);
			if (it2!=elements.end())
				elements.erase(it2);
			wholeObjects.insert(array);
		}
	}
}


inline std::ostream &operator<<(
	std::ostream &os, const IndependentElementSet &ies)
{ ies.print(os); return os; }


typedef std::vector<
	std::pair<ref<Expr>, IndependentElementSet> > worklist_ty;

static IndependentElementSet getIndependentConstraints(
	const Query& query,
	std::vector< ref<Expr> > &result)
{
	IndependentElementSet	eltsClosure(query.expr);
	worklist_ty		worklist;

	foreach (it, query.constraints.begin(), query.constraints.end())
		worklist.push_back(
			std::make_pair(*it, IndependentElementSet(*it)));

	// XXX This should be more efficient
	// (in terms of low level copy stuff).
	bool done = false;
	while (done == false) {
		worklist_ty	newWorklist;

		done = true;
		foreach (it, worklist.begin(), worklist.end()) {
			if (it->second.intersects(eltsClosure)) {
				if (eltsClosure.add(it->second))
					done = false;
				result.push_back(it->first);
			} else {
				newWorklist.push_back(*it);
			}
		}
		worklist.swap(newWorklist);
	}

	return eltsClosure;
}

#if 0
    std::set< ref<Expr> > reqset(result.begin(), result.end());
    std::cerr << "--\n";
    std::cerr << "Q: " << query.expr << "\n";
    std::cerr << "\telts: " << IndependentElementSet(query.expr) << "\n";
    int i = 0;
  foreach (it, query.constraints.begin(), query.constraints.end()) {
      std::cerr << "C" << i++ << ": " << *it;
      std::cerr << " " << (reqset.count(*it) ? "(required)" : "(independent)") << "\n";
      std::cerr << "\telts: " << IndependentElementSet(*it) << "\n";
    }
    std::cerr << "elts closure: " << eltsClosure << "\n";
#endif



class IndependentSolver : public SolverImplWrapper
{
public:
  IndependentSolver(Solver *_solver)
    : SolverImplWrapper(_solver) {}
  virtual ~IndependentSolver() { }

  bool computeSat(const Query&);
  Solver::Validity computeValidity(const Query&);
  ref<Expr> computeValue(const Query&);
  bool computeInitialValues(const Query& query, Assignment& a)
  { return doComputeInitialValues(query, a); }

  void printName(int level = 0) const {
    klee_message("%*s" "IndependentSolver containing:", 2*level, "");
    wrappedSolver->printName(level + 1);
  }
};

#define SETUP_CONSTRAINTS			\
	std::vector< ref<Expr> > required;	\
	IndependentElementSet eltsClosure;	\
	eltsClosure = getIndependentConstraints(query, required);	\
	ConstraintManager tmp(required);	\
	if (isUnconstrained(tmp, query))

static bool isUnconstrained(
	const ConstraintManager& cm, const Query& query)
{
	if (cm.size() != 0)
		return false;

	if (	query.expr->getKind() == Expr::Eq &&
		query.expr->getKid(1)->getKind() == Expr::Read)
	{
		return true;
	}

	if (	query.expr->getKind() == Expr::Extract &&
		query.expr->getKid(0)->getKind() == Expr::Read)
	{
		return true;
	}

	// std::cerr << "MISSED UNCONSTRAINED: " << query.expr << '\n';
	return false;
}

Solver::Validity IndependentSolver::computeValidity(const Query& query)
{
	SETUP_CONSTRAINTS { return Solver::Unknown; }
	return doComputeValidity(Query(tmp, query.expr));
}

bool IndependentSolver::computeSat(const Query& query)
{
	SETUP_CONSTRAINTS { return true; }
	return doComputeSat(Query(tmp, query.expr));
}

ref<Expr> IndependentSolver::computeValue(const Query& query)
{
	SETUP_CONSTRAINTS {
		uint64_t	v;

		v = rand();
		if (query.expr->getWidth() < 64)
			v &= (1 << query.expr->getWidth()) - 1;

		return ConstantExpr::create(
			v, query.expr->getWidth());
	}

	return doComputeValue(Query(tmp, query.expr));
}

Solver *klee::createIndependentSolver(Solver *s)
{ return new Solver(new IndependentSolver(s)); }

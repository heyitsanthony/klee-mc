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

  void add(T x) {
    s.insert(x);
  }
  void add(T start, T end) {
    for (; start<end; start++)
      s.insert(start);
  }

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
inline std::ostream &operator<<(std::ostream &os, const DenseSet<T> &dis) {
  dis.print(os);
  return os;
}

class IndependentElementSet {
  typedef std::map<const Array*, DenseSet<unsigned> > elements_ty;
  elements_ty elements;
  std::set<const Array*> wholeObjects;

public:
  IndependentElementSet() {}
  IndependentElementSet(ref<Expr> e) {
    std::vector< ref<ReadExpr> > reads;
    findReads(e, /* visitUpdates= */ true, reads);
    for (unsigned i = 0; i != reads.size(); ++i) {
      ReadExpr *re = reads[i].get();
      const Array *array = re->updates.root;

      // Reads of a constant array don't alias.
      if (re->updates.root->isConstantArray() &&
          !re->updates.head)
        continue;

      if (!wholeObjects.count(array)) {
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
  }
  IndependentElementSet(const IndependentElementSet &ies) :
    elements(ies.elements),
    wholeObjects(ies.wholeObjects) {}

  IndependentElementSet &operator=(const IndependentElementSet &ies) {
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

  // more efficient when this is the smaller set
  bool intersects(const IndependentElementSet &b) {
    foreach (it, wholeObjects.begin(), wholeObjects.end()) {
      const Array *array = *it;
      if (b.wholeObjects.count(array) ||
          b.elements.find(array) != b.elements.end())
        return true;
    }
    foreach (it, elements.begin(), elements.end()) {
      const Array *array = it->first;
      if (b.wholeObjects.count(array))
        return true;
      elements_ty::const_iterator it2 = b.elements.find(array);
      if (it2 != b.elements.end()) {
        if (it->second.intersects(it2->second))
          return true;
      }
    }
    return false;
  }

  // returns true iff set is changed by addition
  bool add(const IndependentElementSet &b) {
    bool modified = false;
    foreach (it, b.wholeObjects.begin(), b.wholeObjects.end()) {
      const Array *array = *it;
      elements_ty::iterator it2 = elements.find(array);
      if (it2!=elements.end()) {
        modified = true;
        elements.erase(it2);
        wholeObjects.insert(array);
      } else {
        if (!wholeObjects.count(array)) {
          modified = true;
          wholeObjects.insert(array);
        }
      }
    }
    foreach (it, b.elements.begin(), b.elements.end()) {
      const Array *array = it->first;
      if (wholeObjects.count(array)) continue;
      elements_ty::iterator it2 = elements.find(array);
      if (it2==elements.end()) {
        modified = true;
        elements.insert(*it);
      } else {
        if (it2->second.add(it->second))
          modified = true;
      }
    }
    return modified;
  }
};

inline std::ostream &operator<<(std::ostream &os, const IndependentElementSet &ies) {
  ies.print(os);
  return os;
}

static IndependentElementSet getIndependentConstraints(
	const Query& query,
	std::vector< ref<Expr> > &result)
{
  IndependentElementSet eltsClosure(query.expr);
  std::vector< std::pair<ref<Expr>, IndependentElementSet> > worklist;

  foreach (it, query.constraints.begin(), query.constraints.end())
    worklist.push_back(std::make_pair(*it, IndependentElementSet(*it)));

  // XXX This should be more efficient (in terms of low level copy stuff).
  bool done = false;
  do {
    done = true;
    std::vector< std::pair<ref<Expr>, IndependentElementSet> > newWorklist;
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
  } while (!done);

  if (0) {
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
  }

  return eltsClosure;
}

class IndependentSolver : public SolverImplWrapper
{
public:
  IndependentSolver(Solver *_solver)
    : SolverImplWrapper(_solver) {}
  virtual ~IndependentSolver() { }

  bool computeSat(const Query&);
  Solver::Validity computeValidity(const Query&);
  ref<Expr> computeValue(const Query&);
  bool computeInitialValues(
  	const Query& query,
	const std::vector<const Array*> &objects,
	std::vector< std::vector<unsigned char> > &values)
  {
    return doComputeInitialValues(query, objects, values);
  }

  void printName(int level = 0) const {
    klee_message("%*s" "IndependentSolver containing:", 2*level, "");
    wrappedSolver->printName(level + 1);
  }
};

#define SETUP_CONSTRAINTS			\
	std::vector< ref<Expr> > required;	\
	IndependentElementSet eltsClosure;	\
	eltsClosure = getIndependentConstraints(query, required);	\
	ConstraintManager tmp(required);

Solver::Validity IndependentSolver::computeValidity(const Query& query)
{
	SETUP_CONSTRAINTS
	return doComputeValidity(Query(tmp, query.expr));
}

bool IndependentSolver::computeSat(const Query& query)
{
	SETUP_CONSTRAINTS
	return doComputeSat(Query(tmp, query.expr));
}

ref<Expr> IndependentSolver::computeValue(const Query& query)
{
	SETUP_CONSTRAINTS
	return doComputeValue(Query(tmp, query.expr));
}

Solver *klee::createIndependentSolver(Solver *s) {
  return new Solver(new IndependentSolver(s));
}

//===-- Constraints.cpp ---------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Constraints.h"

#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprVisitor.h"
#include "static/Sugar.h"

#include <string.h>
#include <iostream>
#include <list>
#include <map>

using namespace klee;

class ExprReplaceVisitor : public ExprVisitor
{
private:
  ref<Expr> src, dst;

public:
  ExprReplaceVisitor(ref<Expr> _src, ref<Expr> _dst) : src(_src), dst(_dst) {}

  Action visitExpr(const Expr &e) {
    if (e == *src.get()) {
      return Action::changeTo(dst);
    } else {
      return Action::doChildren();
    }
  }

  Action visitExprPost(const Expr &e) {
    if (e == *src.get()) {
      return Action::changeTo(dst);
    } else {
      return Action::doChildren();
    }
  }
};

class ExprReplaceVisitor2 : public ExprVisitor {
private:
  const std::map< ref<Expr>, ref<Expr> > &replacements;

public:
  ExprReplaceVisitor2(const std::map< ref<Expr>, ref<Expr> > &_replacements)
    : ExprVisitor(true),
      replacements(_replacements) {}

  Action visitExprPost(const Expr &e) {
    std::map< ref<Expr>, ref<Expr> >::const_iterator it;
    it = replacements.find(ref<Expr>(const_cast<Expr*>(&e)));
    if (it!=replacements.end()) {
      return Action::changeTo(it->second);
    } else {
      return Action::doChildren();
    }
  }
};

bool ConstraintManager::rewriteConstraints(ExprVisitor &visitor)
{
  ConstraintManager::constraints_ty old;
  bool changed = false;

  constraints.swap(old);
  foreach (it, old.begin(), old.end()) {
    ref<Expr> &ce = *it;
    ref<Expr> e = visitor.visit(ce);

    if (e != ce) {
      addConstraintInternal(e); // enable further reductions
      changed = true;
    } else {
      constraints.push_back(ce);
    }
  }

  return changed;
}

void ConstraintManager::simplifyForValidConstraint(ref<Expr> e)
{
	assert (0 == 1 && "STUB");
}

ref<Expr> ConstraintManager::simplifyExpr(ref<Expr> e) const
{
	if (isa<ConstantExpr>(e)) return e;

	std::map< ref<Expr>, ref<Expr> > equalities;

	foreach(it, constraints.begin(), constraints.end()) {
		const EqExpr *ee = dyn_cast<EqExpr>(*it);
		if (ee && isa<ConstantExpr>(ee->left)) {
			equalities.insert(std::make_pair(ee->right, ee->left));
		} else {
			equalities.insert(std::make_pair(
				*it, ConstantExpr::alloc(1, Expr::Bool)));
		}
	}

	return ExprReplaceVisitor2(equalities).visit(e);
}

bool ConstraintManager::addConstraintInternal(ref<Expr> e)
{
	// rewrite any known equalities, return false if
	// we find ourselves with a contradiction. This means that
	// the constraint we're adding can't happen!

	// XXX should profile the effects of this and the overhead.
	// traversing the constraints looking for equalities is hardly the
	// slowest thing we do, but it is probably nicer to have a
	// ConstraintSet ADT which efficiently remembers obvious patterns
	// (byte-constant comparison).
	switch (e->getKind()) {
	case Expr::Constant:
	//	assert(cast<ConstantExpr>(e)->isTrue() &&
	//	"attempt to add invalid (false) constraint");

		if (!cast<ConstantExpr>(e)->isTrue())
			return false;
		return true;

	// split to enable finer grained independence and other optimizations
	case Expr::And: {
		BinaryExpr *be = cast<BinaryExpr>(e);
		if (!addConstraintInternal(be->left)) return false;
		if (!addConstraintInternal(be->right)) return false;
		return true;
	}

	case Expr::Eq: {
		BinaryExpr *be = cast<BinaryExpr>(e);
		if (isa<ConstantExpr>(be->left)) {
			ExprReplaceVisitor visitor(be->right, be->left);
			rewriteConstraints(visitor);
		}
		constraints.push_back(e);
		return true;
	}

	default:
		constraints.push_back(e);
		return true;
	}

	return true;
}

bool ConstraintManager::addConstraint(ref<Expr> e)
{
	e = simplifyExpr(e);
	return addConstraintInternal(e);
}

void ConstraintManager::print(std::ostream& os) const
{
	for (unsigned int i = 0; i < constraints.size(); i++) {
		constraints[i]->print(os);
		os << "\n";
	}
}

//===-- Constraints.h -------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_CONSTRAINTS_H
#define KLEE_CONSTRAINTS_H

#include "klee/Expr.h"

// FIXME: Currently we use ConstraintManager for two things: to pass
// sets of constraints around, and to optimize constraints. We should
// move the first usage into a separate data structure
// (ConstraintSet?) which ConstraintManager could embed if it likes.
namespace klee {

class ExprVisitor;
class ExprReplaceVisitor2;

class ConstraintManager
{
public:
	typedef std::vector< ref<Expr> > constraints_ty;
	typedef constraints_ty::iterator iterator;
	typedef constraints_ty::const_iterator const_iterator;
	typedef std::vector< ref<Expr> >::const_iterator constraint_iterator;


	ConstraintManager() : simplifier(NULL) {}
	virtual ~ConstraintManager();

	// create from constraints with no optimization
	explicit
	ConstraintManager(const std::vector< ref<Expr> > &_constraints)
	: constraints(_constraints)
	, simplifier(NULL) {}

	ConstraintManager(const ConstraintManager &cs)
	: constraints(cs.constraints)
	, simplifier(NULL) {}

	ConstraintManager& operator=(const ConstraintManager &cs)
	{
		constraints = cs.constraints;
		invalidateSimplifier();
		return *this;
	}

	// given a constraint which is known to be valid, attempt to
	// simplify the existing constraint set
	void simplifyForValidConstraint(ref<Expr> e);

	ref<Expr> simplifyExpr(ref<Expr> e) const;

	bool addConstraint(ref<Expr> e);

	bool empty() const { return constraints.empty(); }
	ref<Expr> back() const { return constraints.back(); }
	constraint_iterator begin() const { return constraints.begin(); }
	constraint_iterator end() const { return constraints.end(); }
	size_t size() const { return constraints.size(); }

	bool operator==(const ConstraintManager &other) const
	{ return constraints == other.constraints; }

	void print(std::ostream& os) const;

private:
	constraints_ty constraints;
	mutable ExprReplaceVisitor2* simplifier;

	// returns true iff the constraints were modified
	bool rewriteConstraints(ExprVisitor &visitor);

	bool addConstraintInternal(ref<Expr> e);
	void invalidateSimplifier(void) const;
};

}

#endif /* KLEE_CONSTRAINTS_H */

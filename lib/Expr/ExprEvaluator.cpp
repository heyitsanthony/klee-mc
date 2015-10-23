//===-- ExprEvaluator.cpp -------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/util/ExprEvaluator.h"
#include <iostream>

using namespace klee;

ExprVisitor::Action ExprEvaluator::evalRead(
	const UpdateList &ul, unsigned index)
{
	for (const UpdateNode *un=ul.head; un; un=un->next) {
		ref<Expr> ui(visit(un->index));

		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(ui)) {
			if (CE->getZExtValue() != index)
				continue;
			return Action::changeTo(visit(un->value));
		}

		// update index is unknown, so may or may not be index, we
		// cannot guarantee value. we can rewrite to read at this
		// version though (mostly for debugging).
		return Action::changeTo(
			ReadExpr::create(
				UpdateList(ul.getRoot(), un),
				MK_CONST(index, ul.getRoot()->getDomain())));
	}

	if (ul.getRoot()->isConstantArray() && index < ul.getRoot()->mallocKey.size)
		return Action::changeTo(ul.getRoot()->getValue(index));

	return Action::changeTo(getInitialValue(ul.getRoot(), index));
}

ExprVisitor::Action ExprEvaluator::evalUpdateList(const ReadExpr &re)
{
	UpdateList	new_ul(re.updates.getRoot(), 0);
	bool		changed = false;

	for (auto un = re.updates.head; un; un = un->next) {
		auto idx = visit(un->index);
		auto v = visit(un->value);

		changed |= idx != un->index || v != un->value;
		new_ul.extend(idx, v);
	}

	if (!changed)
		return Action::doChildren();

	return Action::changeTo(MK_READ(new_ul, visit(re.index)));
}

ExprVisitor::Action ExprEvaluator::visitExpr(const Expr &e)
{
	return Action::doChildren();
}

ExprVisitor::Action ExprEvaluator::visitRead(const ReadExpr &re)
{
	ref<Expr> v(visit(re.index));

	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(v)) {
		return evalRead(re.updates, CE->getZExtValue());
	} else if (re.updates.head) {
		return evalUpdateList(re);
	}

	return Action::doChildren();
}

// we need to check for div by zero during partial evaluation,
// if this occurs then simply ignore the 0 divisor and use the
// original expression.
ExprVisitor::Action ExprEvaluator::protectedDivOperation(const BinaryExpr &e)
{
	ref<Expr> kids[2] = { visit(e.left), visit(e.right) };

	if (kids[1]->isZero()) {
		kids[1] = e.right;
		protected_div = true;
	}

	if (kids[0] != e.left || kids[1] != e.right)
		return Action::changeTo(e.rebuild(kids));

	return Action::skipChildren();
}

ExprVisitor::Action ExprEvaluator::visitUDiv(const UDivExpr &e)
{ return protectedDivOperation(e); }
ExprVisitor::Action ExprEvaluator::visitSDiv(const SDivExpr &e)
{ return protectedDivOperation(e); }
ExprVisitor::Action ExprEvaluator::visitURem(const URemExpr &e)
{ return protectedDivOperation(e); }
ExprVisitor::Action ExprEvaluator::visitSRem(const SRemExpr &e)
{ return protectedDivOperation(e); }

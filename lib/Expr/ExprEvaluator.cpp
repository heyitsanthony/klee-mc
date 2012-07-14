//===-- ExprEvaluator.cpp -------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/util/ExprEvaluator.h"

using namespace klee;

ExprVisitor::Action ExprEvaluator::evalRead(
	const UpdateList &ul, unsigned index)
{
	for (const UpdateNode *un=ul.head; un; un=un->next) {
		ref<Expr> ui(visit(un->index));

		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(ui)) {
			if (CE->getZExtValue() == index)
				return Action::changeTo(visit(un->value));
			continue;
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

ExprVisitor::Action ExprEvaluator::visitExpr(const Expr &e)
{
#if 0
	// Evaluate all constant expressions here,
	// in case they weren't folded in construction.
	//
	// Don't do this for reads, we want them to go to
	// the normal rewrite path.
	unsigned	N = e.getNumKids();

	if (!N || isa<ReadExpr>(e))
		return Action::doChildren();

	/* this only matters if we have an inadequate constant folder
	 * such as with the default builder. I don't think this
	 * should be on every path. --AJR */

	for (unsigned i = 0; i != N; ++i)
		if (e.getKidConst(i)->getKind() != Expr::Constant)
			return Action::doChildren();

	std::cerr << "HEY: " << e << '\n';
	assert (0 == 1 && "SHOULD HAVE FOLDED!!");

	ref<Expr> Kids[3];
	assert(N < 3);
	for (unsigned i = 0; i != N; ++i) {
		Kids[i] = e.getKid(i);
	}

	return Action::changeTo(e.rebuild(Kids));
#endif
	return Action::doChildren();
}

ExprVisitor::Action ExprEvaluator::visitRead(const ReadExpr &re)
{
	ref<Expr> v(visit(re.index));

	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(v))
		return evalRead(re.updates, CE->getZExtValue());

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

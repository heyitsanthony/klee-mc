#include "ExtraOptBuilder.h"

using namespace klee;

ref<Expr> ExtraOptBuilder::Eq(const ref<Expr> &lhs, const ref<Expr> &rhs)
{
	const ConstantExpr	*lhs_ce, *t_ce, *f_ce, *ret_ce;
	ref<Expr>		ret;

	if (rhs->getKind() != Expr::Select)
		return OptBuilder::Eq(lhs, rhs);

	if (lhs->getKind() != Expr::Constant)
		return OptBuilder::Eq(lhs, rhs);
	
	if (	rhs->getKid(1)->getKind() != Expr::Constant ||
		rhs->getKid(2)->getKind() != Expr::Constant)
		return OptBuilder::Eq(lhs, rhs);

	lhs_ce = cast<ConstantExpr>(lhs);
	t_ce = cast<ConstantExpr>(rhs->getKid(1)); 
	f_ce = cast<ConstantExpr>(rhs->getKid(2)); 

	// can lhs possibly equal rhs?
	ret = OrExpr::create(
		EqExpr::create(lhs, rhs->getKid(1)),
		EqExpr::create(lhs, rhs->getKid(2)));

	ret_ce = dyn_cast<ConstantExpr>(ret);
	if (ret_ce == NULL)
		return OptBuilder::Eq(lhs, rhs);

	// lhs can not possibly equal rhs?
	if (ret_ce->isZero())
		return ConstantExpr::create(0, Expr::Bool);
		

	return OptBuilder::Eq(lhs, rhs);
}

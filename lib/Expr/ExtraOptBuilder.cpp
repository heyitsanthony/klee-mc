#include "ExtraOptBuilder.h"

using namespace klee;

ref<Expr> ExtraOptBuilder::Eq(const ref<Expr> &lhs, const ref<Expr> &rhs)
{
	const ConstantExpr	*ret_ce;
	ref<Expr>		ret;

	if (rhs->getKind() != Expr::Select)
		return OptBuilder::Eq(lhs, rhs);

	if (lhs->getKind() != Expr::Constant)
		return OptBuilder::Eq(lhs, rhs);
	
	if (	rhs->getKid(1)->getKind() != Expr::Constant ||
		rhs->getKid(2)->getKind() != Expr::Constant)
		return OptBuilder::Eq(lhs, rhs);

	// can lhs possibly equal rhs?
	ret = MK_OR(MK_EQ(lhs, rhs->getKid(1)), MK_EQ(lhs, rhs->getKid(2)));

	ret_ce = dyn_cast<ConstantExpr>(ret);
	if (ret_ce == NULL)
		return OptBuilder::Eq(lhs, rhs);

	// lhs can not possibly equal rhs?
	if (ret_ce->isZero())
		return MK_CONST(0, Expr::Bool);
		

	return OptBuilder::Eq(lhs, rhs);
}

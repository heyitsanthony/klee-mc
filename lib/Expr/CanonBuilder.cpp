#include "CanonBuilder.h"

using namespace klee;

bool isExprCanonical(ref<Expr> &e)
{
	if (e->getNumKids() < 2)
		return true;

	switch (e->getKind()) {
	case Expr::Concat:
		if (e->getKid(0)->getKind() == Expr::Concat)
			return false;
	case Expr::Add:
	case Expr::Mul:
	case Expr::And:
	case Expr::Or:
	case Expr::Xor:
	case Expr::Eq:
	case Expr::Ne:
		if (	e->getKid(1)->getKind() == Expr::Constant &&
			e->getKid(0)->getKind() != Expr::Constant)
		{
			return false;
		}
	default:
		return true;
	}

	return true;
}

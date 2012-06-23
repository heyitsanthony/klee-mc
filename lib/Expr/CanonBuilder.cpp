#include "CanonBuilder.h"

using namespace klee;

ref<Expr> CanonBuilder::Concat(const ref<Expr>& l, const ref<Expr>& r)
{
	/* balance concats on the right */
	if (l->getKind() == Expr::Concat) {
		ref<Expr>	e1(l->getKid(0));
		ref<Expr>	e2(l->getKid(1));

		return Concat(e1, Concat(e2, r));
	}

	return Base->Concat(l, r);
}


#define DECL_BIN_REF(x)							\
ref<Expr> CanonBuilder::x(const ref<Expr> &l, const ref<Expr> &r)	\
{	\
	if (l->getKind() == Expr::Constant)	\
		return Base->x(l, r);		\
	if (r->getKind() == Expr::Constant)	\
		return Builder->x(r, l);	\
	return Base->x(l, r);			\
}

//	if (l->getKind() > r->getKind())	\
//		return Builder->x(r, l);	\
//	if (l->skeleton() <= r->skeleton())	\
//		return Base->x(l, r);		\


DECL_BIN_REF(Add)
DECL_BIN_REF(Mul)
DECL_BIN_REF(And)
DECL_BIN_REF(Or)
DECL_BIN_REF(Xor)
DECL_BIN_REF(Eq)
DECL_BIN_REF(Ne)

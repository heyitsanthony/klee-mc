#ifndef CANONBUILDER_H
#define CANONBUILDER_H

#include <iostream>
#include "ChainedBuilder.h"

namespace klee
{
bool isExprCanonical(ref<Expr> &e);

template <class T>
class Canonizer : public T
{
public:
	Canonizer() {}
	Canonizer(ExprBuilder* eb) : T(eb) {}
	Canonizer(ExprBuilder* eb, ExprBuilder *eb2) : T(eb,eb2) {}

	virtual ~Canonizer() {}

#define DECL_BIN_REF(x)	\
virtual ref<Expr> x(const ref<Expr> &l, const ref<Expr> &r)

	DECL_BIN_REF(Concat)
	{
	#if 0
		/* balance concats on the right */
		if (l->getKind() == Expr::Concat)
			ref<Expr>	e1(l->getKid(0));
			ref<Expr>	e2(l->getKid(1));

			return Concat(e1, Concat(e2, r));
		}
	#endif
		/* give constants precedence when possible */

		if (l->getKind() == Expr::Concat) {
			/**
			 *      C          C
			 *     / \        / \
			 *   C    y   => 1   C
			 *  / \             / \
			 * 1   x           x   y
			 */
			ref<Expr>	e1(l->getKid(0));
			ref<Expr>	e2(l->getKid(1));

			if (e1->getKind() == Expr::Constant)
				return Concat(e1, Concat(e2, r));
		}

		if (	r->getKind() == Expr::Concat &&
			l->getKind() != Expr::Constant)
		{
			/**
			 *      C             C
			 *     / \           / \
			 *   x    C    =>   C   1
			 *       / \       / \
			 *      y   1     x   y
			 */
			ref<Expr>	e1(r->getKid(0));
			ref<Expr>	e2(r->getKid(1));

			if (e2->getKind() == Expr::Constant)
				return Concat(Concat(l, e1), e2);
		}

		return T::Concat(l, r);
	}

#define DECL_BIN_BODY(x)	\
{	\
if (l->getKind() == Expr::Constant)	\
	return T::x(l, r);		\
if (r->getKind() == Expr::Constant)	\
	return T::x(r, l);		\
return T::x(l, r);			\
}

#define DECL_BIN_F(x)	DECL_BIN_REF(x) DECL_BIN_BODY(x)

	DECL_BIN_F(Add)
	DECL_BIN_F(Mul)
	DECL_BIN_F(And)
	DECL_BIN_F(Or)
	DECL_BIN_F(Xor)
	DECL_BIN_F(Eq)
	DECL_BIN_F(Ne)
#undef DECL_BIN_F
#undef DECL_BIN_REF
#undef DECL_BIN_BODY
};

template <class T>
class CanonVerifier : public T
{
public:
	CanonVerifier() {}
	CanonVerifier(ExprBuilder* eb) : T(eb) {}
	CanonVerifier(ExprBuilder* eb, ExprBuilder *eb2) : T(eb,eb2) {}

	virtual ~CanonVerifier() {}

#define DECL_BIN_BODY_OP(x, y)				\
{							\
	ref<Expr>	e(y);				\
	if (isExprCanonical(e))				\
		return e;				\
	std::cerr << "NOT CANONICAL!! " << e << '\n';	\
	std::cerr << "LHS: " << l << '\n';		\
	std::cerr << "RHS: " << r << '\n';		\
	std::cerr << "OP: " << #x << '\n';		\
	assert (0 == 1 && "Expression violated canonicalization"); \
	return NULL;					\
}

#define DECL_UN_BODY_OP(x, y)				\
{							\
	ref<Expr>	e(y);				\
	if (isExprCanonical(e))				\
		return e;				\
	std::cerr << "NOT CANONICAL!! " << e << '\n';	\
	std::cerr << "LHS: " << l << '\n';		\
	std::cerr << "OP: " << #x << '\n';		\
	assert (0 == 1 && "Expression violated canonicalization"); \
	return NULL;					\
}

	ref<Expr> Select(
		const ref<Expr> &c, const ref<Expr> &l, const ref<Expr> &r)
		DECL_BIN_BODY_OP(Select, T::Select(c, l, r))

	ref<Expr> Extract(
		const ref<Expr> &l, unsigned Offset, Expr::Width W)
		DECL_UN_BODY_OP(Extract, T::Extract(l, Offset, W))
	ref<Expr> ZExt(const ref<Expr> &l, Expr::Width W)
		DECL_UN_BODY_OP(ZExt, T::ZExt(l, W))
	ref<Expr> SExt(const ref<Expr> &l, Expr::Width W)
		DECL_UN_BODY_OP(SExt, T::SExt(l, W))
	ref<Expr> Not(const ref<Expr> &l)
		DECL_UN_BODY_OP(Not, T::Not(l))

#define DECL_BIN_REF(x)	\
virtual ref<Expr> x(const ref<Expr> &l, const ref<Expr> &r)

#define DECL_BIN_BODY(x) DECL_BIN_BODY_OP(x, T::x(l, r))

#define DECL_BIN_F(x)	DECL_BIN_REF(x) DECL_BIN_BODY(x)
	DECL_BIN_F(Concat)
	DECL_BIN_F(Add)
	DECL_BIN_F(Sub)
	DECL_BIN_F(Mul)
	DECL_BIN_F(UDiv)

	DECL_BIN_F(SDiv)
	DECL_BIN_F(URem)
	DECL_BIN_F(SRem)
	DECL_BIN_F(And)
	DECL_BIN_F(Or)
	DECL_BIN_F(Xor)
	DECL_BIN_F(Shl)
	DECL_BIN_F(LShr)
	DECL_BIN_F(AShr)
	DECL_BIN_F(Eq)
	DECL_BIN_F(Ne)
	DECL_BIN_F(Ult)
	DECL_BIN_F(Ule)

	DECL_BIN_F(Ugt)
	DECL_BIN_F(Uge)
	DECL_BIN_F(Slt)
	DECL_BIN_F(Sle)
	DECL_BIN_F(Sgt)
	DECL_BIN_F(Sge)
#undef DECL_BIN_REF
#undef DECL_BIN_F
#undef DECL_BIN_BODY
};


class CanonBuilder : public Canonizer<ChainedEB>
{
public:
	CanonBuilder(ExprBuilder* b1) : Canonizer<ChainedEB>(b1, b1) {}
	CanonBuilder(ExprBuilder* b1, ExprBuilder* b2)
	: Canonizer<ChainedEB>(b1, b2) {}
	virtual ~CanonBuilder() {}
};

class CanonVerified : public CanonVerifier<ChainedEB>
{
public:
	CanonVerified(ExprBuilder* b1)
	: CanonVerifier<ChainedEB>(b1) {}
	virtual ~CanonVerified() {}
};

}

#endif

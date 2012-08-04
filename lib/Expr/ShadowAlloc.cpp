#include "ShadowExpr.h"
#include "ShadowAlloc.h"

using namespace klee;


#define MK_SHADOW1(x,y) new ShadowExpr<x, uint64_t>(shadow_v, y)
#define MK_SHADOW2(x,y,z) new ShadowExpr<x, uint64_t>(shadow_v, y, z)
#define MK_SHADOW3(x,y,z,w) new ShadowExpr<x, uint64_t>(shadow_v, y, z, w)

#define DECL_ALLOC_1(x)						\
ref<Expr> ShadowAlloc::x(const ref<Expr>& src)			\
{	if (!is_shadowing) return ExprAlloc::x(src);		\
	ref<Expr> r(MK_SHADOW1(x##Expr, src));			\
	r->computeHash();		\
	return r;			\
}

DECL_ALLOC_1(NotOptimized)
DECL_ALLOC_1(Not)

#define DECL_ALLOC_2(x)	\
ref<Expr> ShadowAlloc::x(const ref<Expr>& lhs, const ref<Expr>& rhs)	\
{	\
	if (!is_shadowing) return ExprAlloc::x(lhs, rhs);		\
	ref<Expr> r(MK_SHADOW2(x##Expr, lhs, rhs)); \
	r->computeHash();		\
	return r;			\
}

#define DECL_ALLOC_2_DIV(x)		\
ref<Expr> ShadowAlloc::x(const ref<Expr>& lhs, const ref<Expr>& rhs)	\
{						\
	if (rhs->isZero()) {			\
		Expr::errors++;			\
		Expr::errorExpr = lhs;		\
		return lhs;			\
	}					\
	if (!is_shadowing) return ExprAlloc::x(lhs, rhs); \
	ref<Expr> r(MK_SHADOW2(x##Expr, lhs, rhs)); \
	r->computeHash();		\
	return r;			\
}


DECL_ALLOC_2(Concat)
DECL_ALLOC_2(Add)
DECL_ALLOC_2(Sub)
DECL_ALLOC_2(Mul)

DECL_ALLOC_2_DIV(UDiv)
DECL_ALLOC_2_DIV(SDiv)
DECL_ALLOC_2_DIV(URem)
DECL_ALLOC_2_DIV(SRem)

DECL_ALLOC_2(And)
DECL_ALLOC_2(Or)
DECL_ALLOC_2(Xor)
DECL_ALLOC_2(Shl)
DECL_ALLOC_2(LShr)
DECL_ALLOC_2(AShr)
DECL_ALLOC_2(Eq)
DECL_ALLOC_2(Ne)
DECL_ALLOC_2(Ult)
DECL_ALLOC_2(Ule)

DECL_ALLOC_2(Ugt)
DECL_ALLOC_2(Uge)
DECL_ALLOC_2(Slt)
DECL_ALLOC_2(Sle)
DECL_ALLOC_2(Sgt)
DECL_ALLOC_2(Sge)

ref<Expr> ShadowAlloc::Read(const UpdateList &updates, const ref<Expr> &idx)
{
	if (!is_shadowing) return ExprAlloc::Read(updates, idx);
	ref<Expr> r(new ShadowExpr<ReadExpr, uint64_t>(shadow_v, updates, idx));
	r->computeHash();
	return r;
}

ref<Expr> ShadowAlloc::Select(
	const ref<Expr> &c,
	const ref<Expr> &t, const ref<Expr> &f)
{
	if (!is_shadowing) return ExprAlloc::Select(c, t, f);
	ref<Expr> r(MK_SHADOW3(SelectExpr, c, t, f));
	r->computeHash();
	return r;
}

ref<Expr> ShadowAlloc::Extract(const ref<Expr> &e, unsigned o, Expr::Width w)
{
	if (!is_shadowing) return ExprAlloc::Extract(e, o, w);
	ref<Expr> r(MK_SHADOW3(ExtractExpr, e, o, w));
	r->computeHash();
	return r;
}

ref<Expr> ShadowAlloc::ZExt(const ref<Expr> &e, Expr::Width w)
{
	if (!is_shadowing) return ExprAlloc::ZExt(e, w);
	ref<Expr> r(MK_SHADOW2(ZExtExpr, e, w));
	r->computeHash();
	return r;
}

ref<Expr> ShadowAlloc::SExt(const ref<Expr> &e, Expr::Width w)
{
	if (!is_shadowing) return ExprAlloc::SExt(e, w);
	ref<Expr> r(MK_SHADOW2(SExtExpr, e, w));
	r->computeHash();
	return r;
}

ref<Expr> ShadowAlloc::Constant(const llvm::APInt &Value)
{
	if (!is_shadowing) return ExprAlloc::Constant(Value);
	ref<Expr> r(MK_SHADOW1(ConstantExpr, Value));
	r->computeHash();
	return r;
}

ShadowRef ShadowAlloc::getExpr(const ref<Expr>& e)
{
	if (e->isShadowed() == false)
		return NULL;

	return ShadowRef(static_cast<ShadowType*>(e.get()));
}

ShadowRef ShadowAlloc::getExprDynCast(const ref<Expr>& e)
{
	switch (e->getKind()) {
#define CAST_CASE(x)	\
	case Expr::x:		\
	return ShadowRef(	\
		reinterpret_cast<ShadowType*>(	\
			dynamic_cast<ShadowExpr<x##Expr,uint64_t>*>(	\
				e.get())));

	CAST_CASE(Constant)
	CAST_CASE(NotOptimized)
	CAST_CASE(Read)
	CAST_CASE(Select)
	CAST_CASE(Concat)
	CAST_CASE(Extract)

	CAST_CASE(ZExt)
	CAST_CASE(SExt)

	CAST_CASE(Add)
	CAST_CASE(Sub)
	CAST_CASE(Mul)
	CAST_CASE(UDiv)

	CAST_CASE(SDiv)
	CAST_CASE(URem)
	CAST_CASE(SRem)

	CAST_CASE(Not)
	CAST_CASE(And)
	CAST_CASE(Or)
	CAST_CASE(Xor)
	CAST_CASE(Shl)
	CAST_CASE(LShr)
	CAST_CASE(AShr)

	CAST_CASE(Eq)
	CAST_CASE(Ne)
	CAST_CASE(Ult)
	CAST_CASE(Ule)
	CAST_CASE(Ugt)
	CAST_CASE(Uge)
	CAST_CASE(Slt)
	CAST_CASE(Sle)
	CAST_CASE(Sgt)
	CAST_CASE(Sge)
	default: break;
	}
	return NULL;
}

#include "TopLevelBuilder.h"
#include "ShadowBuilder.h"
#include "ShadowAlloc.h"

using namespace klee;

#define SHADOW_BEGIN(x) {				\
	ref<ShadowVal>	old_v(sa->getShadow());		\
	sa->startShadow(x);				\
	taint_c++;

#define SHADOW_END		\
	sa->startShadow(old_v);	}

ExprBuilder* ShadowBuilder::create(ExprBuilder* eb, ShadowMix* _sm)
{
	if (_sm == NULL) _sm = new ShadowMixOr();
	return new TopLevelBuilder(new ShadowBuilder(eb, _sm), eb);
}

ShadowBuilder::ShadowBuilder(ExprBuilder* eb, ShadowMix* _sm)
: eb_default(eb)
, sm(_sm)
, taint_c(0)
{
	ExprAlloc	*alloc = Expr::getAllocator();
	sa = dynamic_cast<ShadowAlloc*>(alloc);
	assert (sa != NULL);
}

const ShadowType* ShadowBuilder::getShadowExpr(const ref<Expr>& e) const
{ return ShadowAlloc::getExpr(e).get(); }

#define DECL_ALLOC_1(x)					\
ref<Expr> ShadowBuilder::x(const ref<Expr>& src)	\
{	const ShadowType	*se(getShadowExpr(src));\
	if (se == NULL) return eb_default->x(src);	\
	ref<Expr>	e;			\
	SHADOW_BEGIN(se->getShadow());		\
	e = eb_default->x(src);			\
	SHADOW_END				\
	return e;				\
}

DECL_ALLOC_1(NotOptimized)
DECL_ALLOC_1(Not)

#define DECL_ALLOC_2_BODY(x)		\
	const ShadowType	*se[2];	\
	ref<ShadowVal>		tag;	\
	se[0] = getShadowExpr(lhs);	\
	se[1] = getShadowExpr(rhs);	\
	if (se[0] == NULL && se[1] == NULL)	\
		return eb_default->x(lhs, rhs);	\
	if (se[1] && se[0])	\
		tag = sm->mix(se[0]->getShadow(), se[1]->getShadow()); \
	else if (se[1]) tag = se[1]->getShadow();	\
	else tag = se[0]->getShadow();		\
	ref<Expr>	e;			\
	SHADOW_BEGIN(tag)			\
	e = eb_default->x(lhs, rhs);		\
	SHADOW_END				\
	return e;				\

#define DECL_ALLOC_2(x)	\
ref<Expr> ShadowBuilder::x(const ref<Expr>& lhs, const ref<Expr>& rhs)	\
{ DECL_ALLOC_2_BODY(x) }

#define DECL_ALLOC_2_DIV(x)	\
ref<Expr> ShadowBuilder::x(const ref<Expr>& lhs, const ref<Expr>& rhs) \
{						\
	if (rhs->isZero()) {			\
		Expr::errors++;			\
		Expr::errorExpr = lhs;		\
		return lhs;			\
	}					\
	DECL_ALLOC_2_BODY(x)			\
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

ref<Expr> ShadowBuilder::Read(const UpdateList &updates, const ref<Expr> &idx)
{
	const ShadowType	*se(getShadowExpr(idx));
	if (se == NULL) return eb_default->Read(updates, idx);
	ref<Expr>	e;
	SHADOW_BEGIN(se->getShadow());
	e = eb_default->Read(updates, idx);
	SHADOW_END
	return e;
}

ref<Expr> ShadowBuilder::Select(
	const ref<Expr> &c,
	const ref<Expr> &t, const ref<Expr> &f)
{

	const ShadowType	*se[3];
	bool			tag_used = false;
	ref<ShadowVal>		tag;

	se[0] = getShadowExpr(c);
	se[1] = getShadowExpr(t);
	se[2] = getShadowExpr(f);
	if (se[0] == NULL && se[1] == NULL && se[2] == NULL)
		return eb_default->Select(c, t, f);

	if (se[0] != NULL) tag = se[0]->getShadow();
	if (se[1] != NULL) {
		tag = (tag_used)
			? sm->mix(tag, se[1]->getShadow())
			: se[1]->getShadow();
		tag_used = true;
	}
	if (se[2] != NULL) {
		tag = (tag_used)
			? sm->mix(tag, se[2]->getShadow())
			: se[2]->getShadow();
		tag_used = true;
	}

	ref<Expr> e;
	SHADOW_BEGIN(tag);
	e = eb_default->Select(c, t, f);
	SHADOW_END
	return e;
}

ref<Expr> ShadowBuilder::Extract(const ref<Expr> &e, unsigned o, Expr::Width w)
{
	const ShadowType	*se;

	se = getShadowExpr(e);
	if (se == NULL) return eb_default->Extract(e, o, w);

	ref<Expr> r;
	SHADOW_BEGIN(se->getShadow());
	r = eb_default->Extract(e, o, w);
	SHADOW_END
	return r;
}

ref<Expr> ShadowBuilder::ZExt(const ref<Expr> &e, Expr::Width w)
{
	const ShadowType	*se;

	se = getShadowExpr(e);
	if (se == NULL) return eb_default->ZExt(e, w);

	ref<Expr> r;
	SHADOW_BEGIN(se->getShadow());
	r = eb_default->ZExt(e, w);
	SHADOW_END
	return r;
}

ref<Expr> ShadowBuilder::SExt(const ref<Expr> &e, Expr::Width w)
{
	const ShadowType	*se;

	se = getShadowExpr(e);
	if (se == NULL) return eb_default->SExt(e, w);

	ref<Expr> r;
	SHADOW_BEGIN(se->getShadow());
	r = eb_default->SExt(e, w);
	SHADOW_END
	return r;
}

ref<Expr> ShadowBuilder::Constant(const llvm::APInt &Value)
{ return eb_default->Constant(Value); }
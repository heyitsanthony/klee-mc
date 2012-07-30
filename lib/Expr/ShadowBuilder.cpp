#include "TopLevelBuilder.h"
#include "ShadowBuilder.h"
#include "ShadowAlloc.h"

using namespace klee;

ExprBuilder* ShadowBuilder::create(ExprBuilder* eb)
{ return new TopLevelBuilder(new ShadowBuilder(eb), eb); }

ShadowBuilder::ShadowBuilder(ExprBuilder* eb)
: eb_default(eb)
{
	ExprAlloc	*alloc = Expr::getAllocator();
	sa = dynamic_cast<ShadowAlloc*>(alloc);
	assert (sa != NULL);
}

const shadowed_ty* ShadowBuilder::getShadowExpr(const ref<Expr>& e) const
{
	const shadowed_ty*	se;
	se = dynamic_cast<const shadowed_ty*>(e.get());
	return se;
}

#define DECL_ALLOC_1(x)					\
ref<Expr> ShadowBuilder::x(const ref<Expr>& src)	\
{	const shadowed_ty	*se(getShadowExpr(src));\
	if (se == NULL) return eb_default->x(src);	\
	sa->startShadow(se->getShadow());		\
	ref<Expr> e(eb_default->x(src));	\
	sa->stopShadow();			\
	return e;				\
}

DECL_ALLOC_1(NotOptimized)
DECL_ALLOC_1(Not)

#define DECL_ALLOC_2_BODY(x)		\
	const shadowed_ty	*se[2];	\
	uint64_t		tag;	\
	se[0] = getShadowExpr(lhs);	\
	se[1] = getShadowExpr(rhs);	\
	if (se[0] == NULL && se[1] == NULL)	\
		return eb_default->x(lhs, rhs);	\
	if (se[1] && se[0])	\
		tag = combine(se[0]->getShadow(), se[1]->getShadow()); \
	else if (se[1]) tag = se[1]->getShadow();	\
	else tag = se[0]->getShadow();		\
	sa->startShadow(tag);			\
	ref<Expr> e(eb_default->x(lhs, rhs));	\
	sa->stopShadow();			\
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
	const shadowed_ty	*se(getShadowExpr(idx));
	if (se == NULL) return eb_default->Read(updates, idx);
	sa->startShadow(se->getShadow());
	ref<Expr> e(eb_default->Read(updates, idx));
	sa->stopShadow();
	return e;
}

ref<Expr> ShadowBuilder::Select(
	const ref<Expr> &c,
	const ref<Expr> &t, const ref<Expr> &f)
{

	const shadowed_ty	*se[3];
	bool			tag_used = false;
	uint64_t		tag;
	
	se[0] = getShadowExpr(c);
	se[1] = getShadowExpr(t);
	se[2] = getShadowExpr(f);
	if (se[0] == NULL && se[1] == NULL && se[2] == NULL)
		return eb_default->Select(c, t, f);

	if (se[0] != NULL) tag = se[0]->getShadow();
	if (se[1] != NULL) {
		tag = (tag_used)
			? combine(tag, se[1]->getShadow())
			: se[1]->getShadow();
		tag_used = true;
	}
	if (se[2] != NULL) {
		tag = (tag_used)
			? combine(tag, se[2]->getShadow())
			: se[2]->getShadow();
		tag_used = true;
	}


	sa->startShadow(tag);
	ref<Expr> e(eb_default->Select(c, t, f));
	sa->stopShadow();
	return e;
}

ref<Expr> ShadowBuilder::Extract(const ref<Expr> &e, unsigned o, Expr::Width w)
{
	const shadowed_ty	*se;

	se = getShadowExpr(e);
	if (se == NULL) return eb_default->Extract(e, o, w);

	sa->startShadow(se->getShadow());
	ref<Expr> r(eb_default->Extract(e, o, w));
	sa->stopShadow();
	return r;
}

ref<Expr> ShadowBuilder::ZExt(const ref<Expr> &e, Expr::Width w)
{
	const shadowed_ty	*se;

	se = getShadowExpr(e);
	if (se == NULL) return eb_default->ZExt(e, w);

	sa->startShadow(se->getShadow());
	ref<Expr> r(eb_default->ZExt(e, w));
	sa->stopShadow();
	return r;
}

ref<Expr> ShadowBuilder::SExt(const ref<Expr> &e, Expr::Width w)
{
	const shadowed_ty	*se;

	se = getShadowExpr(e);
	if (se == NULL) return eb_default->SExt(e, w);

	sa->startShadow(se->getShadow());
	ref<Expr> r(eb_default->SExt(e, w));
	sa->stopShadow();
	return r;
}

ref<Expr> ShadowBuilder::Constant(const llvm::APInt &Value)
{ return eb_default->Constant(Value); }
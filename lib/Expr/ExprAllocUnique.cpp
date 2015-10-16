#include <unordered_set>
#include <assert.h>
#include "klee/Expr.h"
#include "ExprAllocUnique.h"

using namespace klee;

struct hashexpr
{ unsigned operator()(const ref<Expr>& a) const { return a->hash(); } };

struct expreq
{
bool operator()(const ref<Expr>& a, const ref<Expr>& b) const
{
	if (a->hash() != b->hash())
		return false;
	
	if (a->compareDeep(*b.get()))
		return false;

	return true;
}
};

/* important to use an unordered_map instead of a map so we get O(1) access. */
typedef std::unordered_set<ref<Expr>, hashexpr, expreq> ExprTab;

static ExprTab expr_hashtab;

#if 1
ref<Expr> ExprAllocUnique::Constant(const llvm::APInt &v)
{ 
	ref<Expr> r(new ConstantExpr(v));
	r->computeHash();
	return toUnique(r);
}
#endif

#define DECL_ALLOC_1(x)				\
ref<Expr> ExprAllocUnique::x(const ref<Expr>& src)	\
{	\
	ref<Expr> r(new x##Expr(src));	\
	r->computeHash();		\
	return toUnique(r);		\
}

DECL_ALLOC_1(NotOptimized)
DECL_ALLOC_1(Not)

#define DECL_ALLOC_2(x)	\
ref<Expr> ExprAllocUnique::x(const ref<Expr>& lhs, const ref<Expr>& rhs) \
{	\
	ref<Expr> r(new x##Expr(lhs, rhs));	\
	r->computeHash();			\
	return toUnique(r);			\
}
DECL_ALLOC_2(Concat)
DECL_ALLOC_2(Add)
DECL_ALLOC_2(Sub)
DECL_ALLOC_2(Mul)
DECL_ALLOC_2(UDiv)

DECL_ALLOC_2(SDiv)
DECL_ALLOC_2(URem)
DECL_ALLOC_2(SRem)
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

ref<Expr> ExprAllocUnique::Read(const UpdateList &updates, const ref<Expr> &idx)
{
	ref<Expr> r(new ReadExpr(updates, idx));
	r->computeHash();
	return toUnique(r);
}

ref<Expr> ExprAllocUnique::Select(
	const ref<Expr> &c,
	const ref<Expr> &t, const ref<Expr> &f)
{
	ref<Expr> r(new SelectExpr(c, t, f));
	r->computeHash();
	return toUnique(r);
}

ref<Expr> ExprAllocUnique::Extract(const ref<Expr> &e, unsigned o, Expr::Width w)
{
	ref<Expr> r(new ExtractExpr(e, o, w));
	r->computeHash();
	return toUnique(r);
}

ref<Expr> ExprAllocUnique::ZExt(const ref<Expr> &e, Expr::Width w)
{
	ref<Expr> r(new ZExtExpr(e, w));
	r->computeHash();
	return toUnique(r);
}

ref<Expr> ExprAllocUnique::SExt(const ref<Expr> &e, Expr::Width w)
{
	ref<Expr> r(new SExtExpr(e, w));
	r->computeHash();
	return toUnique(r);
}

ref<Expr> ExprAllocUnique::toUnique(ref<Expr>& e)
{
	std::pair<ExprTab::iterator, bool>	p;

	p = expr_hashtab.insert(e);
	return *(p.first);
}

ExprAllocUnique::ExprAllocUnique() { }

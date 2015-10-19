#include "TopLevelBuilder.h"

using namespace klee;

#define DECL_ALLOC_1(x)					\
ref<Expr> TopLevelBuilder::x(const ref<Expr>& src)	\
{	if (in_builder) return eb_recur->x(src);	\
	in_builder = true;		\
	ref<Expr> e(eb_top->x(src));	\
	in_builder = false;		\
	return e;			\
}

DECL_ALLOC_1(NotOptimized)
DECL_ALLOC_1(Not)

#define DECL_ALLOC_2(x)	\
ref<Expr> TopLevelBuilder::x(const ref<Expr>& lhs, const ref<Expr>& rhs)	\
{	\
	if (in_builder) return eb_recur->x(lhs, rhs);	\
	in_builder = true;			\
	ref<Expr> e(eb_top->x(lhs, rhs));	\
	in_builder = false;			\
	return e;				\
}

#define DECL_ALLOC_2_DIV(x)		\
ref<Expr> TopLevelBuilder::x(const ref<Expr>& lhs, const ref<Expr>& rhs)	\
{						\
	if (rhs->isZero()) {			\
		Expr::errors++;			\
		Expr::errorExpr = lhs;		\
		return lhs;			\
	}					\
	if (in_builder) return eb_recur->x(lhs, rhs);	\
	in_builder = true;			\
	ref<Expr> e(eb_top->x(lhs, rhs));	\
	in_builder = false;			\
	return e;				\
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

ref<Expr> TopLevelBuilder::Read(const UpdateList &updates, const ref<Expr> &idx)
{
	if (in_builder) return eb_recur->Read(updates, idx);
	in_builder = true;
	ref<Expr> e(eb_top->Read(updates, idx));
	in_builder = false;
	return e;
}

ref<Expr> TopLevelBuilder::Select(
	const ref<Expr> &c,
	const ref<Expr> &t, const ref<Expr> &f)
{
	if (in_builder) return eb_recur->Select(c, t, f);
	in_builder = true;
	ref<Expr> e(eb_top->Select(c, t, f));
	in_builder = false;
	return e;
}

ref<Expr> TopLevelBuilder::Extract(const ref<Expr> &e, unsigned o, Expr::Width w)
{
	if (in_builder) return eb_recur->Extract(e, o, w);
	in_builder = true;
	ref<Expr> r(eb_top->Extract(e, o, w));
	in_builder = false;
	return r;
}

ref<Expr> TopLevelBuilder::ZExt(const ref<Expr> &e, Expr::Width w)
{
	if (in_builder) return eb_recur->ZExt(e, w);
	in_builder = true;
	ref<Expr> r(eb_top->ZExt(e, w));
	in_builder = false;
	return r;
}

ref<Expr> TopLevelBuilder::SExt(const ref<Expr> &e, Expr::Width w)
{
	if (in_builder) return eb_recur->SExt(e, w);
	in_builder = true;
	ref<Expr> r(eb_top->SExt(e, w));
	in_builder = false;
	return r;
}

ref<Expr> TopLevelBuilder::Constant(uint64_t v, unsigned w)
{
	if (in_builder) return eb_recur->Constant(v, w);
	in_builder = true;
	ref<Expr> r(eb_top->Constant(v, w));
	in_builder = false;
	return r;
}

ref<Expr> TopLevelBuilder::Constant(const llvm::APInt &Value)
{
	if (in_builder) return eb_recur->Constant(Value);
	in_builder = true;
	ref<Expr> r(eb_top->Constant(Value));
	in_builder = false;
	return r;
}

void TopLevelBuilder::printName(std::ostream& os) const
{
	os << "TopLevelBuilder {\n";
	eb_top->printName(os);
	eb_recur->printName(os);
	os << "}\n";
}

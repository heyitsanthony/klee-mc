#ifndef KLEE_EXPRALLOCFASTUNIQUE_H
#define KLEE_EXPRALLOCFASTUNIQUE_H

#include "ExprAlloc.h"

namespace klee
{
/// ExprBuilder - Base expression builder class.
class ExprAllocFastUnique : public ExprAlloc
{
public:
	ExprAllocFastUnique();
	virtual ~ExprAllocFastUnique() {}

	unsigned garbageCollect(void) override;

	int compare(const Expr& lhs, const Expr& rhs) override
	{ return (&lhs == &rhs) ? 0 : ((long)&lhs - (long)&rhs); }

	ref<Expr> NotOptimized(const ref<Expr> &Index) override;
	ref<Expr> Read(const UpdateList &Updates, const ref<Expr> &idx) override;
	ref<Expr> Select(
		const ref<Expr> &Cond,
		const ref<Expr> &LHS, const ref<Expr> &RHS) override;
	ref<Expr> Extract(
		const ref<Expr> &LHS, unsigned Offset, Expr::Width W) override;
	ref<Expr> ZExt(const ref<Expr> &LHS, Expr::Width W) override;
	ref<Expr> SExt(const ref<Expr> &LHS, Expr::Width W) override;

	ref<Expr> Not(const ref<Expr> &LHS) override;
#define DECL_BIN_REF(x)	\
ref<Expr> x(const ref<Expr> &LHS, const ref<Expr> &RHS) override;
	DECL_BIN_REF(Concat)
	DECL_BIN_REF(Add)
	DECL_BIN_REF(Sub)
	DECL_BIN_REF(Mul)
	DECL_BIN_REF(UDiv)

	DECL_BIN_REF(SDiv)
	DECL_BIN_REF(URem)
	DECL_BIN_REF(SRem)
	DECL_BIN_REF(And)
	DECL_BIN_REF(Or)
	DECL_BIN_REF(Xor)
	DECL_BIN_REF(Shl)
	DECL_BIN_REF(LShr)
	DECL_BIN_REF(AShr)
	DECL_BIN_REF(Eq)
	DECL_BIN_REF(Ne)
	DECL_BIN_REF(Ult)
	DECL_BIN_REF(Ule)

	DECL_BIN_REF(Ugt)
	DECL_BIN_REF(Uge)
	DECL_BIN_REF(Slt)
	DECL_BIN_REF(Sle)
	DECL_BIN_REF(Sgt)
	DECL_BIN_REF(Sge)
#undef DECL_BIN_REF
private:
	ref<Expr> toFastUnique(ref<Expr>& e);
	static unsigned long expr_miss_c;
	static unsigned long expr_hit_c;
};
}

#endif

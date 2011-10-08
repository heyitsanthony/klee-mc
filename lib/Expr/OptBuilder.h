#ifndef OPTBUILDER_H
#define OPTBUILDER_H

#include "klee/ExprBuilder.h"

namespace klee
{

class OptBuilder : public ExprBuilder
{
	virtual ref<Expr> Constant(const llvm::APInt &Value)
	{ return ConstantExpr::alloc(Value); }

	virtual ref<Expr> NotOptimized(const ref<Expr> &Index)
	{ return NotOptimizedExpr::alloc(Index); }

	virtual ref<Expr> Read(
		const UpdateList &Updates,
		const ref<Expr> &Index);

	virtual ref<Expr> Select(
		const ref<Expr> &Cond,
		const ref<Expr> &LHS,
		const ref<Expr> &RHS);

	virtual ref<Expr> Extract(
		const ref<Expr> &LHS,
		unsigned Offset,
		Expr::Width W);

	virtual ref<Expr> Not(const ref<Expr> &L)
	{
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(L))
			return CE->Not();

		return NotExpr::alloc(L);
	}

	ref<Expr> Ne(const ref<Expr> &l, const ref<Expr> &r)
	{
		return EqExpr::create(
			ConstantExpr::create(0, Expr::Bool),
			EqExpr::create(l, r));
	}

	ref<Expr> Ugt(const ref<Expr> &l, const ref<Expr> &r)
	{ return UltExpr::create(r, l); }

	ref<Expr> Uge(const ref<Expr> &l, const ref<Expr> &r)
	{ return UleExpr::create(r, l); }

	ref<Expr> Sgt(const ref<Expr> &l, const ref<Expr> &r)
	{ return SltExpr::create(r, l); }

	ref<Expr> Sge(const ref<Expr> &l, const ref<Expr> &r)
	{ return SleExpr::create(r, l); }

	virtual ref<Expr> ZExt(const ref<Expr> &LHS, Expr::Width W);
	virtual ref<Expr> SExt(const ref<Expr> &LHS, Expr::Width W);


#define DECL_BIN_REF(x)	\
virtual ref<Expr> x(const ref<Expr> &LHS, const ref<Expr> &RHS);


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
	DECL_BIN_REF(Ult)
	DECL_BIN_REF(Ule)

	DECL_BIN_REF(Slt)
	DECL_BIN_REF(Sle)
#undef DECL_BIN_REF
};
}

#endif

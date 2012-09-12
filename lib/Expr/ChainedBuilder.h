#ifndef CHAINED_BUILDER_H
#define CHAINED_BUILDER_H

#include "klee/ExprBuilder.h"

namespace klee
{
/// ChainedBuilder - Helper class for construct specialized expression
/// builders, which implements (non-virtual) methods which forward to a base
/// expression builder, for all expressions.
class ChainedBuilder
{
protected:
	/// Builder - The builder that this specialized builder is contained
	/// within. Provided for convenience to clients.
	ExprBuilder * Builder;

	/// Base - The base builder class for constructing expressions.
	ExprBuilder *Base;

public:
	ChainedBuilder(
		ExprBuilder * _Builder,
		ExprBuilder * _Base)
	: Builder(_Builder)
	, Base(_Base)
	{}

	virtual ~ChainedBuilder() { delete Base; }

	ref<Expr> Constant(const llvm::APInt & Value)
	{ return Base->Constant(Value); }

	ref<Expr> NotOptimized(const ref<Expr> &Index)
	{ return Base->NotOptimized(Index); }

	ref<Expr> Read(
		const UpdateList & Updates,
		const ref<Expr> &Index) 
	{ return Base->Read(Updates, Index); }

	ref<Expr> Select(
		const ref<Expr> &Cond,
		const ref<Expr> &LHS,
		const ref<Expr> &RHS)
	{ return Base->Select(Cond, LHS, RHS); }

	ref<Expr> Extract(
		const ref<Expr> &LHS,
		unsigned Offset, Expr::Width W)
	{ return Base->Extract(LHS, Offset, W); }

	ref<Expr> ZExt(const ref<Expr> &LHS, Expr::Width W)
	{ return Base->ZExt(LHS, W); }

	ref<Expr> SExt(const ref<Expr> &LHS, Expr::Width W)
	{ return Base->SExt(LHS, W); }

#define DECL_BIN_REF(x)	\
ref<Expr> x(const ref<Expr> &LHS, const ref<Expr> &RHS) \
{ return Base->x(LHS, RHS); }

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

	ref<Expr> Not(const ref<Expr> &LHS) { return Base->Not(LHS); }
#undef DECL_BIN_REF
};

class ChainedEB : public ExprBuilder
{
protected:
	/// Builder - The builder that this specialized builder is contained
	/// within. Provided for convenience to clients.
	ExprBuilder * Builder;

	/// Base - The base builder class for constructing expressions.
	ExprBuilder *Base;

public:
	ChainedEB(
		ExprBuilder * _Builder,
		ExprBuilder * _Base)
	: Builder(_Builder)
	, Base(_Base)
	{}

	ChainedEB(ExprBuilder * _Builder)
	: Builder(_Builder), Base(_Builder)
	{}


	virtual ~ChainedEB() { delete Base; }

	ref<Expr> Constant(const llvm::APInt & Value)
	{ return Base->Constant(Value); }

	ref<Expr> NotOptimized(const ref<Expr> &Index)
	{ return Base->NotOptimized(Index); }

	ref<Expr> Read(
		const UpdateList & Updates,
		const ref<Expr> &Index) 
	{ return Base->Read(Updates, Index); }

	ref<Expr> Select(
		const ref<Expr> &Cond,
		const ref<Expr> &LHS,
		const ref<Expr> &RHS)
	{ return Base->Select(Cond, LHS, RHS); }

	ref<Expr> Extract(
		const ref<Expr> &LHS,
		unsigned Offset, Expr::Width W)
	{ return Base->Extract(LHS, Offset, W); }

	ref<Expr> ZExt(const ref<Expr> &LHS, Expr::Width W)
	{ return Base->ZExt(LHS, W); }

	ref<Expr> SExt(const ref<Expr> &LHS, Expr::Width W)
	{ return Base->SExt(LHS, W); }

#define DECL_BIN_REF(x)	\
ref<Expr> x(const ref<Expr> &LHS, const ref<Expr> &RHS) \
{ return Base->x(LHS, RHS); }

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

	ref<Expr> Not(const ref<Expr> &LHS) { return Base->Not(LHS); }
#undef DECL_BIN_REF
	virtual void printName(std::ostream& os) const;
};
}

#endif

//===-- ExprBuilder.cpp ---------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/ExprBuilder.h"

using namespace klee;

ExprBuilder::ExprBuilder() {}
ExprBuilder::~ExprBuilder() {}

namespace
{
class DefaultExprBuilder : public ExprBuilder
{
	virtual ref<Expr> Constant(const llvm::APInt &Value)
	{ return ConstantExpr::alloc(Value); }

	virtual ref<Expr> NotOptimized(const ref<Expr> &Index)
	{ return NotOptimizedExpr::alloc(Index); }

	virtual ref<Expr> Read(
		const UpdateList &Updates,
		const ref<Expr> &Index)
	{ return ReadExpr::alloc(Updates, Index); }

	virtual ref<Expr> Select(
		const ref<Expr> &Cond,
		const ref<Expr> &LHS,
		const ref<Expr> &RHS)
	{ return SelectExpr::alloc(Cond, LHS, RHS); }

	virtual ref<Expr> Extract(
		const ref<Expr> &LHS, unsigned Offset, Expr::Width W)
	{ return ExtractExpr::alloc(LHS, Offset, W); }

	virtual ref<Expr> ZExt(const ref<Expr> &LHS, Expr::Width W)
	{ return ZExtExpr::alloc(LHS, W); }

	virtual ref<Expr> SExt(const ref<Expr> &LHS, Expr::Width W)
	{ return SExtExpr::alloc(LHS, W); }

	virtual ref<Expr> Not(const ref<Expr> &L) { return NotExpr::alloc(L); }
#define DECL_BIN_REF(x)	\
virtual ref<Expr> x(const ref<Expr> &LHS, const ref<Expr> &RHS) \
{ return x##Expr::alloc(LHS, RHS); }


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
  };

}

ExprBuilder *klee::createDefaultExprBuilder()
{ return new DefaultExprBuilder(); }



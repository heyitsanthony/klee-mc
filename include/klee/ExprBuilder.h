//===-- ExprBuilder.h -------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_EXPRBUILDER_H
#define KLEE_EXPRBUILDER_H

#include "Expr.h"

namespace klee {
/// ExprBuilder - Base expression builder class.
class ExprBuilder
{
protected:
	ExprBuilder();

public:
	virtual ~ExprBuilder();

	// Expressions
	virtual ref<Expr> Constant(const llvm::APInt &Value) = 0;
	virtual ref<Expr> NotOptimized(const ref<Expr> &Index) = 0;
	virtual ref<Expr> Read(const UpdateList &Updates,
			   const ref<Expr> &Index) = 0;
	virtual ref<Expr> Select(const ref<Expr> &Cond,
			     const ref<Expr> &LHS, const ref<Expr> &RHS) = 0;
	virtual ref<Expr> Extract(const ref<Expr> &LHS,
			      unsigned Offset, Expr::Width W) = 0;
	virtual ref<Expr> ZExt(const ref<Expr> &LHS, Expr::Width W) = 0;
	virtual ref<Expr> SExt(const ref<Expr> &LHS, Expr::Width W) = 0;

	virtual ref<Expr> Not(const ref<Expr> &LHS) = 0;
#define DECL_BIN_REF(x)	\
virtual ref<Expr> x(const ref<Expr> &LHS, const ref<Expr> &RHS) = 0;
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

	// Utility functions
	ref<Expr> False() { return ConstantExpr::alloc(0, Expr::Bool); }
	ref<Expr> True() { return ConstantExpr::alloc(0, Expr::Bool); }

	ref<Expr> Constant(uint64_t Value, Expr::Width W)
	{ return Constant(llvm::APInt(W, Value)); }
};

/// createDefaultExprBuilder - Create an expression builder which does no
/// folding.
ExprBuilder *createDefaultExprBuilder(void);

/// createConstantFoldingExprBuilder - Create an expression builder which
/// folds constant expressions.
///
/// Base - The base builder to use when constructing expressions.
ExprBuilder *createConstantFoldingExprBuilder(ExprBuilder *Base);

/// createSimplifyingExprBuilder - Create an expression builder which attemps
/// to fold redundant expressions and normalize expressions for improved
/// caching.
///
/// Base - The base builder to use when constructing expressions.
ExprBuilder *createSimplifyingExprBuilder(ExprBuilder *Base);
}

#endif

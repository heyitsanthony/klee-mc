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

#include <iostream>
#include "Expr.h"

#define EXPR_BUILDER_DECL_BIN_REF(x)	\
virtual ref<Expr> x(const ref<Expr> &LHS, const ref<Expr> &RHS);

#define EXPR_BUILDER_DECL_ALL	\
	virtual ref<Expr> Constant(const llvm::APInt &Value);	\
	virtual ref<Expr> NotOptimized(const ref<Expr> &Index);	\
	virtual ref<Expr> Read(const UpdateList &Updates, const ref<Expr> &idx); \
	virtual ref<Expr> Select(	\
		const ref<Expr> &Cond,	\
		const ref<Expr> &LHS, const ref<Expr> &RHS);	\
	virtual ref<Expr> Extract(	\
		const ref<Expr> &LHS, unsigned Offset, Expr::Width W);	\
	virtual ref<Expr> ZExt(const ref<Expr> &LHS, Expr::Width W);	\
	virtual ref<Expr> SExt(const ref<Expr> &LHS, Expr::Width W);	\
\
	virtual ref<Expr> Not(const ref<Expr> &LHS);	\
	EXPR_BUILDER_DECL_BIN_REF(Concat)	\
	EXPR_BUILDER_DECL_BIN_REF(Add)	\
	EXPR_BUILDER_DECL_BIN_REF(Sub)	\
	EXPR_BUILDER_DECL_BIN_REF(Mul)	\
	EXPR_BUILDER_DECL_BIN_REF(UDiv)	\
\
	EXPR_BUILDER_DECL_BIN_REF(SDiv)	\
	EXPR_BUILDER_DECL_BIN_REF(URem)	\
	EXPR_BUILDER_DECL_BIN_REF(SRem)	\
	EXPR_BUILDER_DECL_BIN_REF(And)	\
	EXPR_BUILDER_DECL_BIN_REF(Or)	\
	EXPR_BUILDER_DECL_BIN_REF(Xor)	\
	EXPR_BUILDER_DECL_BIN_REF(Shl)	\
	EXPR_BUILDER_DECL_BIN_REF(LShr)	\
	EXPR_BUILDER_DECL_BIN_REF(AShr)	\
	EXPR_BUILDER_DECL_BIN_REF(Eq)	\
	EXPR_BUILDER_DECL_BIN_REF(Ne)	\
\
	EXPR_BUILDER_DECL_BIN_REF(Ult)	\
	EXPR_BUILDER_DECL_BIN_REF(Ule)	\
	EXPR_BUILDER_DECL_BIN_REF(Ugt)	\
	EXPR_BUILDER_DECL_BIN_REF(Uge)	\
	EXPR_BUILDER_DECL_BIN_REF(Slt)	\
	EXPR_BUILDER_DECL_BIN_REF(Sle)	\
	EXPR_BUILDER_DECL_BIN_REF(Sgt)	\
	EXPR_BUILDER_DECL_BIN_REF(Sge)

namespace klee
{
/// ExprBuilder - Base expression builder class.
class ExprBuilder
{
protected:
	ExprBuilder();

public:
	enum BuilderKind {
		DefaultBuilder,
		ConstantFoldingBuilder,
		SimplifyingBuilder,
		HandOptBuilder,
		ExtraOptsBuilder,
		RuleBuilder,
		BitSimplifier
	};

	static ExprBuilder* create(BuilderKind);

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
	ref<Expr> False() { return ConstantExpr::create(0, Expr::Bool); }
	ref<Expr> True() { return ConstantExpr::create(1, Expr::Bool); }

	ref<Expr> Constant(uint64_t Value, Expr::Width W)
	{ return Constant(llvm::APInt(W, Value)); }

	virtual void printName(std::ostream& os) const = 0;
};

/// createDefaultExprBuilder - Create builder which does no folding.
ExprBuilder *createDefaultExprBuilder(void);

/// createConstantFoldingExprBuilder - Create builder which folds constants.
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

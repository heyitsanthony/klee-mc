#ifndef CONSTSPECEXPRBUILDER_H
#define CONSTSPECEXPRBUILDER_H

#include "klee/ExprBuilder.h"

/// ConstantSpecializedExprBuilder - A base expression builder class which
/// handles dispatching to a helper class, based on whether the arguments are
/// constant or not.
///
/// The SpecializedBuilder template argument should be a helper class which
/// implements methods for all the expression construction functions. These
/// methods can be specialized to take [Non]ConstantExpr when desired.
//

namespace klee
{
template<typename SpecializedBuilder>
class ConstantSpecializedExprBuilder : public ExprBuilder
{
SpecializedBuilder Builder;

public:
	ConstantSpecializedExprBuilder(ExprBuilder *Base) : Builder(this, Base) {}
	~ConstantSpecializedExprBuilder() {}

	virtual ref<Expr> Constant(const llvm::APInt &Value)
	{ return Builder.Constant(Value); }

	virtual ref<Expr> NotOptimized(const ref<Expr> &Index)
	{ return Builder.NotOptimized(Index); }

	virtual ref<Expr> Read(
		const UpdateList &Updates,
		const ref<Expr> &Index)
	{
		// Roll back through writes when possible.
		const UpdateNode *UN = Updates.head;
		while (UN && Eq(Index, UN->index)->isFalse())
			UN = UN->next;

		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(Index))
			return Builder.Read(UpdateList(Updates.root, UN), CE);

		return Builder.Read(UpdateList(Updates.root, UN), Index);
	}

	virtual ref<Expr> Select(
		const ref<Expr> &Cond,
		const ref<Expr> &LHS,
		const ref<Expr> &RHS)
	{
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(Cond))
			return CE->isTrue() ? LHS : RHS;

		return Builder.Select(cast<NonConstantExpr>(Cond), LHS, RHS);
	}

	virtual ref<Expr> Extract(
		const ref<Expr> &LHS, unsigned Offset, Expr::Width W)
	{
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(LHS))
			return CE->Extract(Offset, W);

		return Builder.Extract(cast<NonConstantExpr>(LHS), Offset, W);
	}

	virtual ref<Expr> ZExt(const ref<Expr> &LHS, Expr::Width W)
	{
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(LHS))
			return CE->ZExt(W);

		return Builder.ZExt(cast<NonConstantExpr>(LHS), W);
	}

	virtual ref<Expr> SExt(const ref<Expr> &LHS, Expr::Width W)
	{
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(LHS))
			return CE->SExt(W);

		return Builder.SExt(cast<NonConstantExpr>(LHS), W);
	}

	virtual ref<Expr> Not(const ref<Expr> &LHS) {
		// !!X ==> X
		if (NotExpr *DblNot = dyn_cast<NotExpr>(LHS))
			return DblNot->getKid(0);

		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(LHS))
			return CE->Not();

		return Builder.Not(cast<NonConstantExpr>(LHS));
	}


#define DECL_BIN_REF(x)	\
virtual ref<Expr> x(const ref<Expr> &LHS, const ref<Expr> &RHS)	{ \
	if (ConstantExpr *LCE = dyn_cast<ConstantExpr>(LHS)) {	\
		if (ConstantExpr *RCE = dyn_cast<ConstantExpr>(RHS))	\
			return LCE->x(RCE);	\
		return Builder.x(LCE, cast<NonConstantExpr>(RHS));	\
	} else if (ConstantExpr *RCE = dyn_cast<ConstantExpr>(RHS)) {	\
		return Builder.x(cast<NonConstantExpr>(LHS), RCE);	\
	}	\
	return Builder.x(	\
		cast<NonConstantExpr>(LHS),	\
		cast<NonConstantExpr>(RHS));	\
}
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
#endif

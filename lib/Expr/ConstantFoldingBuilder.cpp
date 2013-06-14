#include "ChainedBuilder.h"
#include "ConstantSpecializedExprBuilder.h"

using namespace klee;

namespace klee
{

class ConstantFoldingBuilder : public ChainedBuilder
{
public:
	ConstantFoldingBuilder(ExprBuilder *Builder, ExprBuilder *Base)
	: ChainedBuilder(Builder, Base) {}

	virtual ~ConstantFoldingBuilder() {}

	ref<Expr> Add(
		const ref<ConstantExpr> &LHS,
		const ref<NonConstantExpr> &RHS);

	ref<Expr> Add(
		const ref<NonConstantExpr> &LHS,
		const ref<ConstantExpr> &RHS)
	{ return Add(RHS, LHS); }

	ref<Expr> Add(
		const ref<NonConstantExpr> &LHS,
		const ref<NonConstantExpr> &RHS);

	ref<Expr> Sub(
		const ref<ConstantExpr> &LHS,
		const ref<NonConstantExpr> &RHS);


	ref<Expr> Sub(
		const ref<NonConstantExpr> &LHS,
		const ref<ConstantExpr> &RHS)
	{
		// X - C_0 ==> -C_0 + X
		return Add(RHS->Neg(), LHS);
	}

	ref<Expr> Sub(
		const ref<NonConstantExpr> &LHS,
		const ref<NonConstantExpr> &RHS);

	ref<Expr> Mul(
		const ref<ConstantExpr> &LHS,
		const ref<NonConstantExpr> &RHS)
	{
		if (LHS->isZero())
			return LHS;
		if (LHS->isOne())
			return RHS;
		// FIXME: Unbalance nested muls, fold constants through
		// {sub,add}-with-constant, etc.
		return Base->Mul(LHS, RHS);
	}

	ref<Expr> Mul(
		const ref<NonConstantExpr> &LHS,
		const ref<ConstantExpr> &RHS)
	{ return Mul(RHS, LHS); }

	ref<Expr> Mul(
		const ref<NonConstantExpr> &LHS,
		const ref<NonConstantExpr> &RHS)
	{ return Base->Mul(LHS, RHS); }

	ref<Expr> And(
		const ref<ConstantExpr> &LHS,
		const ref<NonConstantExpr> &RHS)
	{
		if (LHS->isZero())
			return LHS;
		if (LHS->isAllOnes())
			return RHS;
		// FIXME: Unbalance nested ands, fold constants through
		// {and,or}-with-constant, etc.
		return Base->And(LHS, RHS);
	}

	ref<Expr> And(
		const ref<NonConstantExpr> &LHS,
		const ref<ConstantExpr> &RHS)
	{ return And(RHS, LHS); }

	ref<Expr> And(
		const ref<NonConstantExpr> &LHS,
		const ref<NonConstantExpr> &RHS)
	{ return Base->And(LHS, RHS); }

	ref<Expr> Or(
		const ref<ConstantExpr> &LHS,
		const ref<NonConstantExpr> &RHS)
	{
		if (LHS->isZero())
			return RHS;
		if (LHS->isAllOnes())
			return LHS;
		// FIXME: Unbalance nested ors, fold constants through
		// {and,or}-with-constant, etc.
		return Base->Or(LHS, RHS);
	}

	ref<Expr> Or(
		const ref<NonConstantExpr> &LHS,
		const ref<ConstantExpr> &RHS)
	{ return Or(RHS, LHS); }

	ref<Expr> Or(
		const ref<NonConstantExpr> &LHS,
		const ref<NonConstantExpr> &RHS)
	{ return Base->Or(LHS, RHS); }

	ref<Expr> Xor(
		const ref<ConstantExpr> &LHS,
		const ref<NonConstantExpr> &RHS)
	{
		if (LHS->isZero())
			return RHS;
		// FIXME: Unbalance nested ors, fold constants through
		// {and,or}-with-constant, etc.
		return Base->Xor(LHS, RHS);
	}

	ref<Expr> Xor(
		const ref<NonConstantExpr> &LHS,
		const ref<ConstantExpr> &RHS)
	{ return Xor(RHS, LHS); }

	ref<Expr> Xor(
		const ref<NonConstantExpr> &LHS,
		const ref<NonConstantExpr> &RHS)
	{ return Base->Xor(LHS, RHS); }

	ref<Expr> Eq(
		const ref<ConstantExpr> &LHS,
		const ref<NonConstantExpr> &RHS)
	{
		Expr::Width Width = LHS->getWidth();

		if (Width == Expr::Bool) {
			// true == X ==> X
			if (LHS->isTrue())
				return RHS;

			// false == ... (not)
			return Base->Not(RHS);
		}

		return Base->Eq(LHS, RHS);
	}

	ref<Expr> Eq(
		const ref<NonConstantExpr> &LHS,
		const ref<ConstantExpr> &RHS)
	{ return Eq(RHS, LHS); }

	ref<Expr> Eq(
		const ref<NonConstantExpr> &LHS,
		const ref<NonConstantExpr> &RHS)
	{ return Base->Eq(LHS, RHS); }
};

typedef ConstantSpecializedExprBuilder<ConstantFoldingBuilder>
ConstantFoldingExprBuilder;

ExprBuilder *createConstantFoldingExprBuilder(ExprBuilder *Base)
{
	return new ConstantFoldingExprBuilder(Base);
}

ref<Expr> ConstantFoldingBuilder::Add(
	const ref<NonConstantExpr> &LHS,
	const ref<NonConstantExpr> &RHS)
{
	switch (LHS->getKind()) {
	default: break;

	case Expr::Add: {
		BinaryExpr *BE = cast<BinaryExpr>(LHS);
		// (X + Y) + Z ==> X + (Y + Z)
		return Builder->Add(
			BE->left,
			Builder->Add(BE->right, RHS));
		}

	case Expr::Sub: {
		BinaryExpr *BE = cast<BinaryExpr>(LHS);
		// (X - Y) + Z ==> X + (Z - Y)
		return Builder->Add(
			BE->left,
			Builder->Sub(RHS, BE->right));
		}
	}

	switch (RHS->getKind()) {
	default: break;

	case Expr::Add: {
		BinaryExpr *BE = cast<BinaryExpr>(RHS);
		// X + (C_0 + Y) ==> C_0 + (X + Y)
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(BE->left))
			return Builder->Add(CE, Builder->Add(LHS, BE->right));
		// X + (Y + C_0) ==> C_0 + (X + Y)
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(BE->right))
			return Builder->Add(CE, Builder->Add(LHS, BE->left));
		break;
	}

	case Expr::Sub: {
		BinaryExpr *BE = cast<BinaryExpr>(RHS);
		// X + (C_0 - Y) ==> C_0 + (X - Y)
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(BE->left))
			return Builder->Add(
				CE, Builder->Sub(LHS, BE->right));
		// X + (Y - C_0) ==> -C_0 + (X + Y)
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(BE->right))
			return Builder->Add(
				CE->Neg(), Builder->Add(LHS, BE->left));
		break;
		}
	}

	return Base->Add(LHS, RHS);
}


ref<Expr> ConstantFoldingBuilder::Add(
	const ref<ConstantExpr> &LHS,
	const ref<NonConstantExpr> &RHS)
{
	// 0 + X ==> X
	if (LHS->isZero())
		return RHS;

	switch (RHS->getKind()) {
	default: break;

	case Expr::Add: {
		BinaryExpr *BE = cast<BinaryExpr>(RHS);
		// C_0 + (C_1 + X) ==> (C_0 + C1) + X
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(BE->left))
			return Builder->Add(LHS->Add(CE), BE->right);
		// C_0 + (X + C_1) ==> (C_0 + C1) + X
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(BE->right))
			return Builder->Add(LHS->Add(CE), BE->left);
		break;
	}

	case Expr::Sub: {
		BinaryExpr *BE = cast<BinaryExpr>(RHS);
		// C_0 + (C_1 - X) ==> (C_0 + C1) - X
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(BE->left))
			return Builder->Sub(LHS->Add(CE), BE->right);
		// C_0 + (X - C_1) ==> (C_0 - C1) + X
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(BE->right))
			return Builder->Add(LHS->Sub(CE), BE->left);
		break;
	}
	}

	return Base->Add(LHS, RHS);
}

ref<Expr> ConstantFoldingBuilder::Sub(
	const ref<ConstantExpr> &LHS,
	const ref<NonConstantExpr> &RHS)
{
	switch (RHS->getKind()) {
	default: break;

	case Expr::Add: {
		BinaryExpr *BE = cast<BinaryExpr>(RHS);
		// C_0 - (C_1 + X) ==> (C_0 - C1) - X
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(BE->left))
			return Builder->Sub(LHS->Sub(CE), BE->right);
		// C_0 - (X + C_1) ==> (C_0 + C1) + X
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(BE->right))
			return Builder->Sub(LHS->Sub(CE), BE->left);
		break;
	}

	case Expr::Sub: {
		BinaryExpr *BE = cast<BinaryExpr>(RHS);
		// C_0 - (C_1 - X) ==> (C_0 - C1) + X
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(BE->left))
			return Builder->Add(LHS->Sub(CE), BE->right);
		// C_0 - (X - C_1) ==> (C_0 + C1) - X
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(BE->right))
			return Builder->Sub(LHS->Add(CE), BE->left);
		break;
	}
	}

	return Base->Sub(LHS, RHS);
}

ref<Expr> ConstantFoldingBuilder::Sub(
	const ref<NonConstantExpr> &LHS,
	const ref<NonConstantExpr> &RHS)
{
	switch (LHS->getKind()) {
	default: break;

	case Expr::Add: {
		BinaryExpr *BE = cast<BinaryExpr>(LHS);
		// (X + Y) - Z ==> X + (Y - Z)
		return Builder->Add(BE->left, Builder->Sub(BE->right, RHS));
	}

	case Expr::Sub: {
		BinaryExpr *BE = cast<BinaryExpr>(LHS);
		// (X - Y) - Z ==> X - (Y + Z)
		return Builder->Sub(BE->left, Builder->Add(BE->right, RHS));
	}
	}

	switch (RHS->getKind()) {
	default: break;

	case Expr::Add: {
		BinaryExpr *BE = cast<BinaryExpr>(RHS);
		// X - (C + Y) ==> -C + (X - Y)
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(BE->left))
			return Builder->Add(
				CE->Neg(), Builder->Sub(LHS, BE->right));
		// X - (Y + C) ==> -C + (X - Y)
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(BE->right))
			return Builder->Add(
				CE->Neg(), Builder->Sub(LHS, BE->left));
		break;
	}

	case Expr::Sub: {
		BinaryExpr *BE = cast<BinaryExpr>(RHS);
		// X - (C - Y) ==> -C + (X + Y)
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(BE->left))
			return Builder->Add(
				CE->Neg(), Builder->Add(LHS, BE->right));
		// X - (Y - C) ==> C + (X - Y)
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(BE->right))
			return Builder->Add(CE, Builder->Sub(LHS, BE->left));
		break;
	}
	}

	return Base->Sub(LHS, RHS);
}

}

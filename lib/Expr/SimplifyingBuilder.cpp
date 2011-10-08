#include "ConstantSpecializedExprBuilder.h"
#include "ChainedBuilder.h"

using namespace klee;

class SimplifyingBuilder : public ChainedBuilder
{
public:
	SimplifyingBuilder(ExprBuilder *Builder, ExprBuilder *Base)
		: ChainedBuilder(Builder, Base) {}

	ref<Expr> Eq(
		const ref<ConstantExpr> &LHS,
		const ref<NonConstantExpr> &RHS)
	{
		Expr::Width Width = LHS->getWidth();
		
		if (Width == Expr::Bool) {
			// true == X ==> X
			if (LHS->isTrue())
				return RHS;

			// false == X (not)
			return Base->Not(RHS);
		}

		return Base->Eq(LHS, RHS);
	}

	ref<Expr> Eq(
		const ref<NonConstantExpr> &LHS,
		const ref<ConstantExpr> &RHS) { return Eq(RHS, LHS); }

	ref<Expr> Eq(
		const ref<NonConstantExpr> &LHS,
		const ref<NonConstantExpr> &RHS)
	{
		// X == X ==> true
		if (LHS == RHS)
			return Builder->True();

		return Base->Eq(LHS, RHS);
	}

	ref<Expr> Not(const ref<NonConstantExpr> &LHS) {
		// Transform !(a or b) ==> !a and !b.
		if (const OrExpr *OE = dyn_cast<OrExpr>(LHS))
			return Builder->And(
				Builder->Not(OE->left),
				Builder->Not(OE->right));
		return Base->Not(LHS);
	}

	ref<Expr> Ne(const ref<Expr> &LHS, const ref<Expr> &RHS) {
		// X != Y ==> !(X == Y)
		return Builder->Not(Builder->Eq(LHS, RHS));
	}

	ref<Expr> Ugt(const ref<Expr> &LHS, const ref<Expr> &RHS) {
		// X u> Y ==> Y u< X
		return Builder->Ult(RHS, LHS);
	}

	ref<Expr> Uge(const ref<Expr> &LHS, const ref<Expr> &RHS) {
		// X u>= Y ==> Y u<= X
		return Builder->Ule(RHS, LHS);
	}

	ref<Expr> Sgt(const ref<Expr> &LHS, const ref<Expr> &RHS) {
		// X s> Y ==> Y s< X
		return Builder->Slt(RHS, LHS);
	}

	ref<Expr> Sge(const ref<Expr> &LHS, const ref<Expr> &RHS) {
		// X s>= Y ==> Y s<= X
		return Builder->Sle(RHS, LHS);
	}
};

typedef ConstantSpecializedExprBuilder<SimplifyingBuilder>
	SimplifyingExprBuilder;

ExprBuilder *klee::createSimplifyingExprBuilder(ExprBuilder *Base) {
  return new SimplifyingExprBuilder(Base);
}

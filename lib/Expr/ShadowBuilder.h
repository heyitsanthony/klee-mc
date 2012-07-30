#ifndef SHADOWBUILDER_H
#define SHADOWBUILDER_H

#include "ShadowExpr.h"
#include "klee/ExprBuilder.h"

namespace klee
{
class ShadowAlloc;

typedef ShadowExpr<Expr, uint64_t>	shadowed_ty;
class ShadowBuilder : public ExprBuilder
{
public:
	static ExprBuilder* create(ExprBuilder* default_builder);
	virtual ~ShadowBuilder() { /* eb_default is deleted by TLBuiler */ }

	EXPR_BUILDER_DECL_ALL
protected:
	const shadowed_ty* getShadowExpr(const ref<Expr>& e) const;
	ShadowBuilder(ExprBuilder* eb);
	/* XXX: think this out */
	virtual uint64_t combine(uint64_t a, uint64_t b) { return a; } 
private:
	ShadowAlloc*	sa;
	ExprBuilder*	eb_default;
};
}

#endif

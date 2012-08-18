#ifndef SHADOWALLOC_H
#define SHADOWALLOC_H

#include "ShadowExpr.h"
#include "ExprAlloc.h"
#include <iostream>
namespace klee {
typedef uint64_t ShadowVal;
typedef ShadowExpr<Expr, ShadowVal> ShadowType;
typedef ref<ShadowType> ShadowRef;

class ShadowAlloc : public ExprAlloc
{
public:
	ShadowAlloc() : is_shadowing(false) {}
	virtual ~ShadowAlloc() {}
	void startShadow(ShadowVal v) { is_shadowing = true; shadow_v = v; }
	void stopShadow(void) { is_shadowing = false; }
	bool isShadowing(void) const { return is_shadowing; }
	ShadowVal getShadow(void) const { return shadow_v; }

	static ShadowRef getExprDynCast(const ref<Expr>& e);
	static ShadowRef getExpr(const ref<Expr>& e);

	static ShadowAlloc* get(void)
	{ return static_cast<ShadowAlloc*>(Expr::getAllocator()); }

	EXPR_BUILDER_DECL_ALL

private:
	bool		is_shadowing;
	ShadowVal	shadow_v;
};
}

#endif

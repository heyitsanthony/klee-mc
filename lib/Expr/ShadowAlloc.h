#ifndef SHADOWALLOC_H
#define SHADOWALLOC_H

#include "ShadowExpr.h"
#include "ExprAlloc.h"

namespace klee {
typedef ShadowExpr<Expr, uint64_t> ShadowType;
typedef ref<ShadowType> ShadowRef;

class ShadowAlloc : public ExprAlloc
{
public:
	ShadowAlloc() : is_shadowing(false) {}
	virtual ~ShadowAlloc() {}
	void startShadow(uint64_t v) { is_shadowing = true; shadow_v = v; }
	void stopShadow(void) { is_shadowing = false; }

	static ShadowRef getExprDynCast(const ref<Expr>& e);
	static ShadowRef getExpr(const ref<Expr>& e);

	EXPR_BUILDER_DECL_ALL	

private:
	bool		is_shadowing;
	uint64_t	shadow_v;
};
}

#endif

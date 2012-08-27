#ifndef SHADOWALLOC_H
#define SHADOWALLOC_H

#include "ShadowExpr.h"
#include "ShadowVal.h"
#include "klee/Expr.h"
#include "ExprAlloc.h"
#include <iostream>

namespace klee
{
typedef ShadowExpr<Expr, ref<ShadowVal> > ShadowType;
typedef ref<ShadowType> ShadowRef;

#define SAVE_SHADOW	{					\
	ShadowAlloc	*_sa = ShadowAlloc::get();		\
	ref<ShadowVal>	_old_v;					\
	_old_v = _sa->getShadow();

#define PUSH_SHADOW(x)		\
	SAVE_SHADOW		\
	_sa->startShadow(x);

#define POP_SHADOW \
	_sa->startShadow(_old_v);	}

class ShadowAlloc : public ExprAlloc
{
public:
	ShadowAlloc() {}
	virtual ~ShadowAlloc() {}
	void startShadow(ref<ShadowVal> v)
	{ if (!v.isNull()) v->chk(); shadow_v = v; }

	void stopShadow(void) { shadow_v = NULL; }
	bool isShadowing(void) const { return (shadow_v.isNull() == false); }
	ref<ShadowVal> getShadow(void) const { return shadow_v; }

	static ShadowRef getExprDynCast(const ref<Expr>& e);
	static ShadowRef getExpr(const ref<Expr>& e);
	static const ref<ShadowVal> getExprShadow(const ref<Expr>& e);

	static ShadowAlloc* get(void)
	{ return static_cast<ShadowAlloc*>(Expr::getAllocator()); }

	static ref<Expr> drop(const ref<Expr>& e)
	{
		ref<Expr>	r;

		if (e->isShadowed() == false)
			return e;

		SAVE_SHADOW
		_sa->stopShadow();
		r = e->realloc();
		assert (!r->isShadowed());
		POP_SHADOW
		return r;
	}

	EXPR_BUILDER_DECL_ALL

private:
	ref<ShadowVal>	shadow_v;
};
}

#endif

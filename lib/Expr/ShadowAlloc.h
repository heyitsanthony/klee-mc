#ifndef SHADOWALLOC_H
#define SHADOWALLOC_H

#include "ShadowExpr.h"
#include "klee/Expr.h"
#include "ExprAlloc.h"
#include <iostream>
namespace klee {
typedef ref<Expr> ShadowVal;
#define SHADOW_ARG2TAG(x) x
//typedef uint64_t ShadowVal;
//#define SHADOW_ARG2TAG(x) cast<klee::ConstantExpr>(x)->getZExtValue();

typedef ShadowExpr<Expr, ShadowVal> ShadowType;
typedef ref<ShadowType> ShadowRef;

#define SAVE_SHADOW	{					\
	ShadowAlloc	*_sa = ShadowAlloc::get();		\
	ShadowVal	_old_v;					\
	bool		_was_shadow = _sa->isShadowing();	\
	if (_was_shadow) _old_v = _sa->getShadow();		\

#define PUSH_SHADOW(x)		\
	SAVE_SHADOW		\
	_sa->startShadow(x);

#define POP_SHADOW \
	if (!_was_shadow) _sa->stopShadow();	\
	else _sa->startShadow(_old_v);	}



class ShadowAlloc : public ExprAlloc
{
public:
	ShadowAlloc() : is_shadowing(false) {}
	virtual ~ShadowAlloc() {}
	void startShadow(ShadowVal v)
	{  CHK_SHADOW_V(v); is_shadowing = true; shadow_v = v; }
	void stopShadow(void) { is_shadowing = false; }
	bool isShadowing(void) const { return is_shadowing; }
	ShadowVal getShadow(void) const { return shadow_v; }

	static ShadowRef getExprDynCast(const ref<Expr>& e);
	static ShadowRef getExpr(const ref<Expr>& e);

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
	bool		is_shadowing;
	ShadowVal	shadow_v;
};
}

#endif

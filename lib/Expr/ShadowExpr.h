#ifndef SHADOWEXPR_H
#define SHADOWEXPR_H

#include "klee/Expr.h"

/* acts like a normal expression, but has shadow data! */
namespace klee
{
#define CHK_SHADOW_V(x) assert (x->chk())
template<class T, class V>
class ShadowExpr : public T
{
public:
	ShadowExpr(const V& _s, const ref<Expr>& lhs)
	: T(lhs), shadow_val(_s) { CHK_SHADOW_V(_s); }
	ShadowExpr(const V& _s, const ref<Expr>& lhs, const ref<Expr>& rhs)
	: T(lhs, rhs), shadow_val(_s)
	{ CHK_SHADOW_V(_s); }

	ShadowExpr(
		const V& _s,
		const UpdateList &_updates,
		const ref<Expr> &_index)
	: T(_updates, _index), shadow_val(_s)
	{ CHK_SHADOW_V(_s); }

	ShadowExpr(
		const V& _s,
		const ref<Expr> &e, unsigned b, Expr::Width w)
	: T(e, b, w) , shadow_val(_s)
	{ CHK_SHADOW_V(_s); }

	ShadowExpr(
		const V& _s,
		const ref<Expr> &c, const ref<Expr> &t, const ref<Expr> &f)
	: T(c, t, f), shadow_val(_s)
	{ CHK_SHADOW_V(_s); }

	ShadowExpr(const V& _s, const ref<Expr>& e, Expr::Width w)
	: T(e,w), shadow_val(_s)
	{ CHK_SHADOW_V(_s); }

	ShadowExpr(const V& _s, const llvm::APInt &Value)
	: T(Value), shadow_val(_s)
	{ CHK_SHADOW_V(_s); }

	virtual ~ShadowExpr() {}
	const V& getShadow(void) const { return *((V*)getShadowPtr()); }

	bool isShadowed(void) const { return true; }

	void* getShadowPtr(void) const { return (void*)&shadow_val; }
protected:
	void setShadow(const V& v) { shadow_val = v; }
private:
	V	shadow_val;
};
}

#endif
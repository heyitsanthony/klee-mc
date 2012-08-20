#ifndef SHADOWBUILDER_H
#define SHADOWBUILDER_H

#include "ShadowAlloc.h"
#include "klee/ExprBuilder.h"

namespace klee
{

class ShadowMix
{
public:
	virtual ~ShadowMix() {}
	ShadowVal mix(const ShadowVal& a, const ShadowVal& b)
	{
		ShadowVal	v;
		SAVE_SHADOW
		_sa->stopShadow();
		v = join(a, b);
		POP_SHADOW
		return v;
	}
protected:
	virtual ShadowVal join(
		const ShadowVal& a, const ShadowVal& b) = 0;
	ShadowMix(void) {}
};

class ShadowMixOr : public ShadowMix
{ virtual ShadowVal join(
	const ShadowVal& a, const ShadowVal& b) { return a | b; } };

class ShadowMixAnd : public ShadowMix
{ virtual ShadowVal join(
	const ShadowVal& a, const ShadowVal& b) { return a & b; } };


class ShadowBuilder : public ExprBuilder
{
public:
	static ExprBuilder* create(
		ExprBuilder* default_builder,
		ShadowMix* sm = NULL);
	virtual ~ShadowBuilder() { delete sm; }

	uint64_t getTaintCount(void) const { return taint_c; }

	EXPR_BUILDER_DECL_ALL
protected:
	const ShadowType* getShadowExpr(const ref<Expr>& e) const;
	ShadowBuilder(ExprBuilder* eb, ShadowMix* _sm);
private:
	ShadowAlloc*	sa;
	ExprBuilder*	eb_default;
	ShadowMix	*sm;
	uint64_t	taint_c;
};
}

#endif

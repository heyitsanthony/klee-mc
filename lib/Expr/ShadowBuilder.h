#ifndef SHADOWBUILDER_H
#define SHADOWBUILDER_H

#include "ShadowAlloc.h"
#include "klee/ExprBuilder.h"

namespace klee
{

class ShadowCombine
{
public:
	virtual ~ShadowCombine() {}
	virtual uint64_t combine(uint64_t a, uint64_t b) = 0;
protected:
	ShadowCombine(void) {}
};

class ShadowCombineOr : public ShadowCombine
{ virtual uint64_t combine(uint64_t a, uint64_t b) { return a | b; } };

class ShadowBuilder : public ExprBuilder
{
public:
	static ExprBuilder* create(
		ExprBuilder* default_builder,
		ShadowCombine* sc = NULL);
	virtual ~ShadowBuilder() { delete sc; }

	EXPR_BUILDER_DECL_ALL
protected:
	const ShadowType* getShadowExpr(const ref<Expr>& e) const;
	ShadowBuilder(ExprBuilder* eb, ShadowCombine* _sc);
private:
	ShadowAlloc*	sa;
	ExprBuilder*	eb_default;
	ShadowCombine	*sc;
};
}

#endif

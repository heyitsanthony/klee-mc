#ifndef CANONBUILDER_H
#define CANONBUILDER_H

#include "ChainedBuilder.h"

namespace klee
{

class CanonBuilder : public ChainedBuilder
{
public:
	CanonBuilder(ExprBuilder* b1, ExprBuilder* b2)
	: ChainedBuilder(b1, b2) {}
	virtual ~CanonBuilder() {}
#define DECL_BIN_REF(x)	\
virtual ref<Expr> x(const ref<Expr> &LHS, const ref<Expr> &RHS);
	DECL_BIN_REF(Concat)
	DECL_BIN_REF(Add)
	DECL_BIN_REF(Mul)
	DECL_BIN_REF(And)
	DECL_BIN_REF(Or)
	DECL_BIN_REF(Xor)
	DECL_BIN_REF(Eq)
	DECL_BIN_REF(Ne)
#undef DECL_BIN_REF
};
}

#endif

#ifndef EXTRAOPTBUILDER_H
#define EXTRAOPTBUILDER_H

#include "OptBuilder.h"

namespace klee
{

class ExtraOptBuilder : public OptBuilder
{
#define DECL_BIN_REF(x)	\
virtual ref<Expr> x(const ref<Expr> &LHS, const ref<Expr> &RHS);
	DECL_BIN_REF(Eq)
#undef DECL_BIN_REF

};
}

#endif

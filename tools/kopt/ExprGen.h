#ifndef EXPRGEN_H
#define EXPRGEN_H

#include "klee/Expr.h"

namespace klee
{
class RNG;

class ExprGen
{
public:
	virtual ~ExprGen(void) {}
	static ref<Expr> genExpr(ref<Expr> base, ref<Array> arr, unsigned ops);
	static ref<Expr> genExpr(
		RNG& rng,
		ref<Expr> base, ref<Array> arr, unsigned ops);
protected:
	ExprGen(void) {}

private:
};
}
#endif
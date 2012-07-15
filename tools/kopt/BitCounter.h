#ifndef BITCOUNTER_H
#define BITCOUNTER_H

#include "klee/util/ExprVisitor.h"

namespace klee
{
class BitCounter : public ExprConstVisitor
{
public:
	BitCounter(void) : ExprConstVisitor(false) {}

	unsigned countBits(const ref<Expr>& expr)
	{
		bit_c = 0;
		apply(expr);
		return bit_c;
	}

	virtual Action visitExpr(const Expr* expr)
	{
		bit_c += expr->getWidth();
		return Expand;
	}

private:
	unsigned	bit_c;
};
}
#endif

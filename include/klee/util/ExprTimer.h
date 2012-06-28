#ifndef KLEE_EXPRTIMER_H
#define KLEE_EXPRTIMER_H

#include "klee/util/ExprVisitor.h"
#include <iostream>

namespace klee
{
template <class T>
class ExprTimer : public T
{
public:
	ExprTimer(unsigned _max_iter)
	: cur_iter(0), max_iter(_max_iter) {}

	ExprTimer(
		const ref<Expr>& e1, const ref<Expr>& e2,
		unsigned _max_iter)
	: T(e1, e2)
	, cur_iter(0)
	, max_iter(_max_iter) {}

	virtual ref<Expr> apply(const ref<Expr>& e)
	{
		ref<Expr>	res;
		cur_iter = 0;
		res = T::apply(e);
		if (cur_iter > max_iter)
			return NULL;
		return res;
	}

	unsigned getCost(void) const { return cur_iter; }
protected:
	virtual typename T::Action visitExpr(const Expr& e)
	{
		if (cur_iter > max_iter)
			return T::Action::skipChildren();

		cur_iter++;
		return T::visitExpr(e);
	}

private:
	ExprTimer() {}
	unsigned	cur_iter;
	unsigned	max_iter;
};
}

#endif

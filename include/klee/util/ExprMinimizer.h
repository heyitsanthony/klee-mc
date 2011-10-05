#ifndef EXPRMINIMIZER_H
#define EXPRMINIMIZER_H

#include <list>
#include <set>
#include "klee/Expr.h"
#include "klee/util/ExprVisitor.h"

namespace klee
{

class ExprMinimizer : private ExprVisitor
{
public:
	virtual ~ExprMinimizer() {}

	static ref<Expr> minimize(const ref<Expr>& in);

	// returns a bare expression with the let's stripped out
	// this is useful if you want to run a runtime stack-based
	// visitor but don't want.
	static ref<Expr> minimize(
		const ref<Expr>& in,
		std::list<ref<Expr> >& lets);

protected:
	ExprMinimizer()
	: ExprVisitor(true /* recursive */, false /* ignore constants */)
	{ use_hashcons = false; }

  	Action visitExprPost(const Expr &e);
private:
	Action handlePostReadExpr(const ReadExpr* re);

	typedef ExprHashMap< ref</*Let*/Expr> > bindings_ty;

	std::list<ref<Expr> >		useful_lets_list;

	// let bindings
	bindings_ty	bindings;
};

}

#endif

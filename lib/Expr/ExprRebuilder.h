#ifndef EXPRREBUILDER_H
#define EXPRREBUILDER_H

#include "klee/util/ExprVisitor.h"
#include <stack>

namespace klee
{
class ExprRebuilder : public ExprConstVisitor
{
public:
	ExprRebuilder(void) : ExprConstVisitor(false) {}
	virtual ~ExprRebuilder(void) {}

	ref<Expr> rebuild(const ref<Expr>& expr) { return rebuild(expr.get()); }
	ref<Expr> rebuild(const Expr* expr);
protected:
	virtual Action visitExpr(const Expr* expr);
	virtual void visitExprPost(const Expr* expr);
private:
	typedef std::vector<std::vector<ref<Expr> > > postexprs_ty;
	postexprs_ty			postexprs;
	unsigned			depth;
};
}
#endif

#ifndef EXPRREBUILDER_H
#define EXPRREBUILDER_H

#include "klee/util/ExprVisitor.h"
#include <stack>

namespace klee
{

class ExprRedo : public ExprConstVisitor
{
public:
	ExprRedo(void) : ExprConstVisitor(false) {}
	virtual ~ExprRedo(void) {}

	ref<Expr> rebuild(const ref<Expr>& expr) { return rebuild(expr.get()); }
	ref<Expr> rebuild(const Expr* expr);
protected:
	virtual Action visitExpr(const Expr* expr);

	typedef std::vector<std::vector<ref<Expr> > > postexprs_ty;
	postexprs_ty			postexprs;
	unsigned			depth;
};

class ExprRebuilder : public ExprRedo
{
public:
	ExprRebuilder(void) : ExprRedo() {}
	virtual ~ExprRebuilder(void) {}
protected:
	virtual void visitExprPost(const Expr* expr);
};

class ExprRealloc : public ExprRedo
{
public:
	ExprRealloc(void) : ExprRedo() {}
	virtual ~ExprRealloc(void) {}
protected:
	virtual void visitExprPost(const Expr* expr);
};


}
#endif

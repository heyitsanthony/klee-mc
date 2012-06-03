//===-- ExprVisitor.h -------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_EXPRVISITOR_H
#define KLEE_EXPRVISITOR_H

#include <stack>
#include "ExprHashMap.h"

namespace klee
{

class ExprConstVisitor
{
public:
	typedef std::pair<
		const Expr*, bool /* t=to open/to close=f */> exprvis_ty;
	enum Action { Skip=0, Stop=1, Expand=2, Close=3 };
	virtual ~ExprConstVisitor() {}
	void apply(const Expr* expr);
	void apply(const ref<Expr>& expr);
protected:
	virtual Action visitExpr(const Expr* expr) = 0;
	virtual void visitExprPost(const Expr* expr) {}
	ExprConstVisitor(bool visit_ul=true)
	: visit_update_lists(visit_ul) {}
private:
	bool processHead(std::stack<exprvis_ty>& );
	bool visit_update_lists;
};

class ExprVisitor
{
protected:
	// typed variant, but non-virtual for efficiency
	class Action {
	public:
		enum Kind { SkipChildren=0, DoChildren=1, ChangeTo=2 };

	private:
		//      Action() {}
		Action(Kind _kind)
		: kind(_kind), argument()
		{
			if (constantZero.isNull())
				constantZero = ConstantExpr::alloc(
					0, Expr::Bool);
			argument = constantZero;
		}

		Action(Kind _kind, const ref<Expr> &_argument)
		: kind(_kind), argument(_argument) {}

		static ref<Expr> constantZero;

		friend class ExprVisitor;

	public:
		Kind		kind;
		ref<Expr>	argument;

		Action(const Action& a)
		: kind(a.kind), argument(a.argument) {}

		static Action changeTo(const ref<Expr> &expr)
		{ return Action(ChangeTo,expr); }
		static Action doChildren() { return Action(DoChildren); }
		static Action skipChildren() { return Action(SkipChildren); }
	};

protected:
	explicit
	ExprVisitor(bool _recursive=false, bool in_visitConstants=false);

	virtual Action visitExpr(const Expr&);
	virtual Action visitExprPost(const Expr&);

#define VISIT_ACTION(x)	\
	virtual Action visit##x(const x##Expr&)
	VISIT_ACTION(NotOptimized);
	VISIT_ACTION(Read);
	VISIT_ACTION(Select);
	VISIT_ACTION(Concat);
	VISIT_ACTION(Extract);
	VISIT_ACTION(ZExt);
	VISIT_ACTION(SExt);
	VISIT_ACTION(Add);
	VISIT_ACTION(Sub);
	VISIT_ACTION(Mul);
	VISIT_ACTION(UDiv);
	VISIT_ACTION(SDiv);
	VISIT_ACTION(URem);
	VISIT_ACTION(SRem);
	VISIT_ACTION(Not);
	VISIT_ACTION(And);
	VISIT_ACTION(Or);
	VISIT_ACTION(Xor);
	VISIT_ACTION(Shl);
	VISIT_ACTION(LShr);
	VISIT_ACTION(AShr);
	VISIT_ACTION(Eq);
	VISIT_ACTION(Ne);
	VISIT_ACTION(Ult);
	VISIT_ACTION(Ule);
	VISIT_ACTION(Ugt);
	VISIT_ACTION(Uge);
	VISIT_ACTION(Slt);
	VISIT_ACTION(Sle);
	VISIT_ACTION(Sgt);
	VISIT_ACTION(Sge);
	VISIT_ACTION(Bind);
	VISIT_ACTION(Constant);

	bool use_hashcons;

	// apply the visitor to the expression and return a possibly
	// modified new expression.
	ref<Expr> visit(const ref<Expr> &e);
	virtual Action visitAction(const Expr &ep);
private:
	typedef ExprHashMap< ref<Expr> > visited_ty;
	visited_ty visited;
	bool recursive;
	bool visitConstants;

	ref<Expr> handleActionDoChildren(Expr& ep);

public:
	virtual ~ExprVisitor() {}
	virtual ref<Expr> apply(const ref<Expr>& e) { return visit(e); }

	ref<Expr> buildUpdateStack(
		const UpdateList	&ul,
		ref<Expr>		&readIndex,
		std::stack<std::pair<ref<Expr>, ref<Expr> > >& updateStack,
		bool	&rebuildUpdates);
};
}

#endif

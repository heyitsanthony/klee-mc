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
	enum Action { Skip, Stop, Expand, Close };
	virtual ~ExprConstVisitor() {}
	void visit(const Expr* expr);
	void visit(const ref<Expr>& expr);
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
		enum Kind { SkipChildren, DoChildren, ChangeTo };

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
		Kind kind;
		ref<Expr> argument;

		static Action changeTo(const ref<Expr> &expr)
		{ return Action(ChangeTo,expr); }
		static Action doChildren() { return Action(DoChildren); }
		static Action skipChildren() { return Action(SkipChildren); }
	};

protected:
	explicit
	ExprVisitor(bool _recursive=false, bool in_visitConstants=false);
	virtual ~ExprVisitor() {}

	virtual Action visitExpr(const Expr&);
	virtual Action visitExprPost(const Expr&);

	virtual Action visitNotOptimized(const NotOptimizedExpr&);
	virtual Action visitRead(const ReadExpr&);
	virtual Action visitSelect(const SelectExpr&);
	virtual Action visitConcat(const ConcatExpr&);
	virtual Action visitExtract(const ExtractExpr&);
	virtual Action visitZExt(const ZExtExpr&);
	virtual Action visitSExt(const SExtExpr&);
	virtual Action visitAdd(const AddExpr&);
	virtual Action visitSub(const SubExpr&);
	virtual Action visitMul(const MulExpr&);
	virtual Action visitUDiv(const UDivExpr&);
	virtual Action visitSDiv(const SDivExpr&);
	virtual Action visitURem(const URemExpr&);
	virtual Action visitSRem(const SRemExpr&);
	virtual Action visitNot(const NotExpr&);
	virtual Action visitAnd(const AndExpr&);
	virtual Action visitOr(const OrExpr&);
	virtual Action visitXor(const XorExpr&);
	virtual Action visitShl(const ShlExpr&);
	virtual Action visitLShr(const LShrExpr&);
	virtual Action visitAShr(const AShrExpr&);
	virtual Action visitEq(const EqExpr&);
	virtual Action visitNe(const NeExpr&);
	virtual Action visitUlt(const UltExpr&);
	virtual Action visitUle(const UleExpr&);
	virtual Action visitUgt(const UgtExpr&);
	virtual Action visitUge(const UgeExpr&);
	virtual Action visitSlt(const SltExpr&);
	virtual Action visitSle(const SleExpr&);
	virtual Action visitSgt(const SgtExpr&);
	virtual Action visitSge(const SgeExpr&);
	virtual Action visitBind(const BindExpr&);
	virtual Action visitConstant(const ConstantExpr&);
	bool use_hashcons;

private:
	typedef ExprHashMap< ref<Expr> > visited_ty;
	visited_ty visited;
	bool recursive;
	bool visitConstants;

	ref<Expr> visitActual(const ref<Expr> &e);
	ref<Expr> handleActionDoChildren(Expr& ep);

public:
	// apply the visitor to the expression and return a possibly
	// modified new expression.
	ref<Expr> visit(const ref<Expr> &e);

	ref<Expr> buildUpdateStack(
		const UpdateList	&ul,
		ref<Expr>		&readIndex,
		std::stack<std::pair<ref<Expr>, ref<Expr> > >& updateStack,
		bool	&rebuildUpdates);
};

}

#endif

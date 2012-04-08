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

private:
	typedef ExprHashMap< ref<Expr> > visited_ty;
	visited_ty visited;
	bool recursive;
	bool visitConstants;

	ref<Expr> visitActual(const ref<Expr> &e);
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

/* tags parts of expression that changed */
template <class T>
class ExprVisitorTagger : public T
{
public:
	typedef std::vector<unsigned>	tags_ty;

	ExprVisitorTagger() {}
	virtual ~ExprVisitorTagger() {}

	virtual ref<Expr> apply(const ref<Expr>& e)
	{
		tag_pre = 0;
		tag_post = 0;
		tags_pre.clear();
		tags_post.clear();
		return T::apply(e);
	}

	virtual ExprVisitor::Action visitExpr(const Expr& e)
	{
		tag_pre++;
		ExprVisitor::Action a = T::visitExpr(e);
		if (a.kind == ExprVisitor::Action::ChangeTo)
			tag();
		return a;
	}

	virtual ExprVisitor::Action visitExprPost(const Expr& e)
	{
		tag_post++;
		ExprVisitor::Action	a = T::visitExprPost(e);
		if (a.kind == ExprVisitor::Action::ChangeTo)
			tag();
		return a;
	}

	tags_ty::const_iterator beginPre(void) const
	{ return tags_pre.begin(); }
	tags_ty::const_iterator endPre(void) const
	{ return tags_pre.end(); }
	tags_ty::const_iterator beginPost(void) const
	{ return tags_post.begin(); }
	tags_ty::const_iterator endPost(void) const
	{ return tags_post.end(); }

protected:
	void tag(void) {
		tags_post.push_back(tag_post);
		tags_pre.push_back(tag_pre);
	}

private:
	unsigned	tag_pre, tag_post;
	tags_ty		tags_post, tags_pre;
};

}

#endif

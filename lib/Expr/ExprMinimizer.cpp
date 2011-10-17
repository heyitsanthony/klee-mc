#include "static/Sugar.h"
#include <iostream>
#include "klee/util/ExprMinimizer.h"
#include <algorithm>

using namespace klee;

ref<Expr> ExprMinimizer::minimize(const ref<Expr>& in)
{
	ExprMinimizer	m;
	ref<Expr>	out(m.visit(in));

	if (m.useful_lets_list.empty())
		return out;

	std::reverse(m.useful_lets_list.begin(), m.useful_lets_list.end());
	foreach (it, m.useful_lets_list.begin(), m.useful_lets_list.end())
		out = cast<LetExpr>((*it))->rescope(out);

	return out;
}

ref<Expr> ExprMinimizer::minimize(
	const ref<Expr>& in,
	std::list<ref<Expr> >& lets)
{
	ExprMinimizer	m;
	ref<Expr>	out(m.visit(in));

	lets = m.useful_lets_list;
	return out;
}

ExprVisitor::Action ExprMinimizer::visitExprPost(const Expr &e)
{
	bindings_ty::iterator	it;
	ref<Expr>		let_expr;
	ref<Expr>		cur_e(const_cast<Expr*>(&e));
	bool			changed_kids;

	if (dyn_cast<BindExpr>(cur_e))
		return Action::skipChildren();

	it = bindings.find(ref<Expr>(cur_e));
	if (it != bindings.end()) {
		ref<Expr>	useful_let((*it).second);

		return Action::changeTo(
			BindExpr::alloc(cast<LetExpr>(useful_let)));
	}

	/* rebuild with bound kids */
	ref<Expr>	kids[8];
	changed_kids = false;
	for (unsigned i = 0; i < cur_e->getNumKids(); i++) {
		kids[i] = cur_e->getKid(i);
		it = bindings.find(kids[i]);
		if (it != bindings.end()) {
			kids[i] = BindExpr::alloc(cast<LetExpr>((*it).second));
			changed_kids = true;
		}
	}

	if (changed_kids) {
		cur_e = cur_e->rebuild(kids);

		/* does child-replaced expression already exist? */
		it = bindings.find(ref<Expr>(cur_e));
		if (it != bindings.end()) {
			ref<Expr>	useful_let((*it).second);

			return Action::changeTo(
				BindExpr::alloc(cast<LetExpr>(useful_let)));
		}
	}

/* this is disabled since manipulating the update list may incur
 * result in a new array, which will confuse the SMT Printer.
 * XXX fix idiot SMT printer */
#if 0
	const ReadExpr		*re;
	if ((re = dyn_cast<ReadExpr>(cur_e)) != NULL)
		return handlePostReadExpr(re);
#endif

	/* we don't know the expression to bind the let
	 * to yet, so for now we'll have a placeholder */
	let_expr = LetExpr::alloc(cur_e, ConstantExpr::create(0, 1));
	bindings.insert(std::make_pair(cur_e, let_expr));
	useful_lets_list.push_back(let_expr);


	if (!changed_kids)
		return Action::doChildren();

	return Action::changeTo(cur_e);
}

ExprVisitor::Action ExprMinimizer::handlePostReadExpr(const ReadExpr* re)
{
	std::stack<std::pair<ref<Expr>, ref<Expr> > > updateStack;
	ref<Expr>	uniform;
	ref<Expr>	new_idx;
	ref<Expr>	let_expr;
	bool		rebuildUpdates;
	UpdateList	*newUpdates;

	/* create new update stack */
	new_idx = visit(re->index);
	uniform = buildUpdateStack(
		re->updates,
		new_idx,
		updateStack,
		rebuildUpdates);

	if (!uniform.isNull())
		return Action::changeTo(uniform);

	if (!rebuildUpdates) 
		return Action::doChildren();

	/* make update list from update stack */
	newUpdates = UpdateList::fromUpdateStack(
		re->updates.root, updateStack);

	ref<Expr> new_re;
	
	/* build new read expr */
	new_re = ReadExpr::create(*newUpdates, new_idx);
	delete newUpdates;

	let_expr = LetExpr::alloc(
		new_re,
		ConstantExpr::create(0, 1));
	bindings.insert(std::make_pair(new_re, let_expr));

	return Action::changeTo(new_re);
}



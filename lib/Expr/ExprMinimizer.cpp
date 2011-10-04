#include "static/Sugar.h"
#include <iostream>
#include "klee/util/ExprMinimizer.h"
#include <algorithm>

using namespace klee;

ref<Expr> ExprMinimizer::minimize(const ref<Expr>& in)
{
	ExprMinimizer	m;
	ref<Expr>	out(m.visit(in));

	std::cerr << "\n===========[MIN][MIN][MIN]=========================\n";
	if (m.useful_lets.size() == 0)
		return out;

	std::reverse(m.useful_lets_list.begin(), m.useful_lets_list.end());
	foreach (it, m.useful_lets_list.begin(), m.useful_lets_list.end())
		out = cast<LetExpr>((*it))->rescope(out);

	std::cerr << "\n===========[MIN][MIN][MIN]=========================\n";
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
	const ReadExpr		*re;
	ref<Expr>		cur_e(const_cast<Expr*>(&e));

	if (dyn_cast<BindExpr>(cur_e))
		return Action::skipChildren();

	it = bindings.find(ref<Expr>(cur_e));
	if (it != bindings.end()) {
		ref<Expr>	useful_let((*it).second);

		std::cerr << "[MIN] IN: ";
		cur_e->print(std::cerr);
		std::cerr << "\n[MIN] MATCHED EXISTING: ";
		((*it).first)->print(std::cerr);
		std::cerr << "\n";

		if (useful_lets.count(useful_let) == 0) {
			useful_lets.insert(useful_let);
			useful_lets_list.push_back(useful_let);
		}
		return Action::changeTo(
			BindExpr::alloc(cast<LetExpr>(useful_let)));
	} else {
		std::cerr << "[MIN] NOT FOUND: ";
		cur_e->print(std::cerr);
		std::cerr << '\n';
	}

#if 0
	if ((re = dyn_cast<ReadExpr>(cur_e)) != NULL)
		return handlePostReadExpr(re);
#endif

	/* we don't know the expression to bind the let
	 * to yet, so for now we'll have a placeholder */
	std::cerr << "[MIN] ADDING ";
	cur_e->print(std::cerr);
	std::cerr << "\n";
	let_expr = LetExpr::alloc(cur_e, ConstantExpr::create(0, 1));
	bindings.insert(std::make_pair(cur_e, let_expr));

	return Action::doChildren();
}

ExprVisitor::Action ExprMinimizer::handlePostReadExpr(const ReadExpr* re)
{
	std::stack<std::pair<ref<Expr>, ref<Expr> > > updateStack;
	ref<Expr>	uniform;
	ref<Expr>	new_idx;
	ref<Expr>	let_expr;
	bool		rebuildUpdates;
	UpdateList	*newUpdates;

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

	newUpdates = UpdateList::fromUpdateStack(
		re->updates.root, updateStack);

	ref<Expr> new_re;
	
	new_re = ReadExpr::create(*newUpdates, new_idx);
	let_expr = LetExpr::alloc(
		new_re,
		ConstantExpr::create(0, 1));
	bindings.insert(std::make_pair(new_re, let_expr));

	return Action::changeTo(new_re);
}



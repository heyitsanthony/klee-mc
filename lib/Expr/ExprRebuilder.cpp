#include "klee/Expr.h"
#include "ExprRebuilder.h"

using namespace klee;

ref<Expr> ExprRebuilder::rebuild(const Expr* e)
{
	depth = 0;
	postexprs.clear();
	apply(e);
	return postexprs[0][0];
}

ExprRebuilder::Action ExprRebuilder::visitExpr(const Expr* expr)
{
	depth++;
	if (depth > postexprs.size())
		postexprs.resize(depth);
	return Expand;
}

void ExprRebuilder::visitExprPost(const Expr* expr)
{
	ref<Expr>			post_e;

	depth--;
	assert (postexprs.size() > depth);
	if (postexprs.size() == depth+1) {
		/* no need to rebuild */
		postexprs[depth].push_back(ref<Expr>(const_cast<Expr*>(expr)));
		return;
	}

	/* need to rebuild this expression? */
	assert (postexprs.size() > depth+1);

	/* rewrite arguments to use old ones if possible */
	for (unsigned i = 0; i < expr->getNumKids(); i++) {
		if (	postexprs[depth+1][i]->hash() !=
			expr->getKidConst(i)->hash())
		{
			continue;
		}
		/* use old expression */
		postexprs[depth+1][i] = expr->getKid(i);
	}

	post_e = expr->rebuild(postexprs[depth+1].data());
	if (post_e->hash() != expr->hash())
		postexprs[depth].push_back(post_e);
	else
		postexprs[depth].push_back(
			ref<Expr>(const_cast<Expr*>(expr)));

	/* done with all expressions in subtree, remove the postexprs */
	postexprs.resize(depth+1);
}
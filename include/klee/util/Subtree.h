#ifndef EXPRSUBTREE_H
#define EXPRSUBTREE_H

#include "klee/util/ExprVisitor.h"

namespace klee
{
/* used to iterate subtrees in expressions */
class Subtree : public ExprVisitor
{
public:
	static std::pair<ref<Expr>, ref<Expr> >
		getSubtree(ref<Array>& a, ref<Expr>& e, unsigned i)
	{
		Subtree		st;
		ref<Expr>	ret;

		st.target_node = i;
		st.arr = a;
		st.done = false;
		ret = st.apply(e);
		if (!st.done)
			return std::make_pair(ref<Expr>(0), ref<Expr>(0));
		return std::make_pair(ret, st.sub);
	}

	static unsigned getTag(ref<Array>& a, ref<Expr>& e, unsigned i)
	{
		ExprVisitorTagger<Subtree>	st;
		st.target_node = i;
		st.arr = a;
		st.done = false;
		st.apply(e);
		assert (st.getPreTags().size() == 1);
		return st.getPreTags()[0];
	}

	virtual ~Subtree() {}
protected:
	Subtree()
	: ExprVisitor(false, true)
	, arr(0)
	, cur_node(0)
	{ use_hashcons = false;	}
	virtual Action visitRead(const ReadExpr& re)
	{ return Action::skipChildren(); }

	virtual Action visitReadExpr(const ReadExpr& e)
	{ return Action::skipChildren(); }

	virtual Action visitExpr(const Expr& e)
	{
		if (done) return Action::skipChildren();

		if (cur_node == target_node) {
			ref<Expr>	repl;
			repl = Expr::createTempRead(arr, e.getWidth());
			done = true;
			sub = const_cast<Expr*>(&e);
			return Action::changeTo(
				NotOptimizedExpr::create(repl));
		}

		cur_node++;
		return Action::doChildren();
	}

	virtual Action visitNotOptimizedExpr(const NotOptimizedExpr& e)
	{return Action::skipChildren(); }
private:
	ref<Array>	arr;
	unsigned	cur_node;
	unsigned	target_node;
	bool		done;
	ref<Expr>	sub;
};
}

#endif
#ifndef EXPRTAG_H
#define EXPRTAG_H
#include "klee/util/ExprVisitor.h"

namespace klee
{
typedef std::vector<unsigned>	exprtags_ty;

/* tags parts of expression that changed */
template <class T>
class ExprVisitorTagger : public T
{
public:
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

	virtual typename T::Action visitAction(const Expr& e)
	{
		tag_pre++;
		typename T::Action a(T::visitAction(e));
		if (a.kind == T::Action::ChangeTo)
			tags_pre.push_back(tag_pre);
		return a;
	}

	virtual typename T::Action visitExprPost(const Expr& e)
	{
		tag_post++;
		typename T::Action	a(T::visitExprPost(e));
		if (a.kind == T::Action::ChangeTo)
			tags_post.push_back(tag_post);
		return a;
	}

	const exprtags_ty& getPostTags(void) const { return tags_post; }
	const exprtags_ty& getPreTags(void) const { return tags_pre; }
protected:
private:
	unsigned	tag_pre, tag_post;
	exprtags_ty	tags_post, tags_pre;
};

template <class T>
class ExprVisitorTags : public T
{
public:
	ExprVisitorTags(
		const exprtags_ty& _tags_pre,
		const exprtags_ty& _tags_post)
	: tags_pre(_tags_pre)
	, tags_post(_tags_post) { }

	virtual void apply(const ref<Expr>& e)
	{
		tag_pre = 0;
		tag_post = 0;
		pre_it = tags_pre.begin();
		post_it = tags_post.end();
		T::apply(e);
	}

	virtual typename T::Action visitExpr(const Expr* e)
	{
		tag_pre++;
		if (pre_it != tags_pre.end() && *pre_it == tag_pre) {
			pre_it++;
			return preTagVisit(e);
		}

		return T::visitExpr(e);
	}

	virtual void visitExprPost(const Expr* e)
	{
		tag_post++;
		if (post_it != tags_post.end() && *post_it == tag_post) {
			post_it++;
			postTagVisit(e);
			return;
		}

		T::visitExprPost(e);
	}

protected:
	virtual typename T::Action preTagVisit(const Expr* e) = 0;
	virtual void postTagVisit(const Expr* e) {}

	unsigned			tag_pre, tag_post;
private:
	const exprtags_ty		&tags_pre, &tags_post;
	exprtags_ty::const_iterator	pre_it, post_it;
};

class ExprGetTag : public ExprVisitorTags<ExprConstVisitorNull>
{
public:
	static ref<Expr> getExpr(
		const ref<Expr>& e, int n, bool _skip_reads=false, bool _skip_notopt=false)
	{
		exprtags_ty	pre(1, n), post(0);
		ExprGetTag	egt(pre, post);

		egt.skip_reads = _skip_reads;
		egt.skip_notopts = _skip_notopt;
		egt.ret_expr = NULL;
		egt.apply(e);
		return egt.ret_expr;
	}
	virtual ~ExprGetTag() {}
protected:
	ExprGetTag(const exprtags_ty& pre, const exprtags_ty& post)
	: ExprVisitorTags<ExprConstVisitorNull>(pre, post)
	{}
	Action preTagVisit(const Expr* e)
	{
		ret_expr = ref<Expr>(const_cast<Expr*>(e));
		return Stop;
	}

	virtual Action visitExpr(const Expr* e)
	{
		if (skip_reads && e->getKind() == Expr::Read) {
			tag_pre++;
			return Skip;
		}

		if (skip_notopts && e->getKind() == Expr::NotOptimized) {
			tag_pre++;
			return Skip;
		}

		return ExprVisitorTags<ExprConstVisitorNull>::visitExpr(e);
	}
private:
	bool		skip_reads, skip_notopts;
	ref<Expr>	ret_expr;
};

}
#endif

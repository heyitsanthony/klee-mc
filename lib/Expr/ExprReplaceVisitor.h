#ifndef EXPRREPLACE_VISITOR_H
#define EXPRREPLACE_VISITOR_H

#include "klee/util/ExprVisitor.h"

namespace klee
{
class ExprReplaceVisitor : public ExprVisitor
{
private:
	const ref<Expr> src, dst;

public:
	ExprReplaceVisitor(
		const ref<Expr> &_src,
		const ref<Expr> &_dst)
	: src(_src), dst(_dst) {}

	Action visitExpr(const Expr &e) {
		return (e == *src.get())
			? Action::changeTo(dst)
			: Action::doChildren();
	}

	Action visitExprPost(const Expr &e) {
		return (e == *src.get())
			? Action::changeTo(dst)
			: Action::doChildren();
	}
};
}

#endif

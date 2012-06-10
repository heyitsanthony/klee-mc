#ifndef EXPRFINDLABELS_H
#define EXPRFINDLABELS_H

#include "klee/util/ExprVisitor.h"
#include "Pattern.h"

namespace klee
{
class ExprRule;

class ExprPatternMatch : public ExprConstVisitor
{
public:
	/* this is so that we can get rule application to works for both
	 * tries and a single rule */
	class RuleIterator
	{
	public:
		virtual bool isDone(void) const = 0;
		virtual void reset(void) = 0;
		virtual bool matchValue(uint64_t v) = 0;
		virtual bool skipValue(void) = 0;
		virtual bool matchLabel(uint64_t& v) = 0;
		virtual const ExprRule* getExprRule(void) const = 0;
		virtual ~RuleIterator() {}
		virtual void dump(void) {}
	protected:
		RuleIterator() {}
	private:
	};


	ExprPatternMatch(
		RuleIterator	&_rule_it,
		labelmap_ty	&_lm)
	: rule_it(_rule_it)
	, lm(_lm) {}

	virtual ~ExprPatternMatch() {}

	/* Call this! */
	bool match(const ref<Expr>& e);

protected:
	virtual Action visitExpr(const Expr* expr);
private:
	bool verifyConstant(const ConstantExpr* ce);
	bool verifyConstant(uint64_t v, unsigned w);
	Action matchLabel(const Expr* expr, uint64_t label_op);
	Action matchCLabel(const Expr* expr, uint64_t label_op);


	RuleIterator		&rule_it;
	labelmap_ty		&lm;		/* read label map */
	labelmap_ty		clm;		/* const label map */
	bool			success;
};

}

#endif

#ifndef KNOCKOUTRULE_H
#define KNOCKOUTRULE_H

#include "klee/util/Ref.h"

namespace klee
{
class Solver;
class ExprRule;
class Expr;
class Array;
class KnockOut;
class KnockoutRule
{
public:
	KnockoutRule(const ExprRule* _er, const Array* ko_arr);
	virtual ~KnockoutRule(void);
	bool knockedOut(void) const { return ko.isNull() == false; }
	ref<Expr> getKOExpr(void) const;
	const ExprRule* getExprRule(void) const { return er; }
	ExprRule* createRule(Solver* s) const;
private:
	bool isConstInvariant(Solver* s) const;
	bool isRangedRuleValid(
		Solver* s,
		int& slot,
		ref<Expr>& e_range) const;

	void getRange(
		const ref<Expr>& src,
		ref<Expr>& lo, ref<Expr>& hi) const;


	ExprRule* createFullRule(Solver* s) const;
	ExprRule* createPartialRule(Solver* s) const;


	const ExprRule	*er;
	const Array	*arr;
	ref<Expr>	ko;
	KnockOut	*kout;
};
}

#endif

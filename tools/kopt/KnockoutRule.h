#ifndef KNOCKOUTRULE_H
#define KNOCKOUTRULE_H

#include "klee/util/ExprTag.h"
#include "klee/util/Ref.h"

namespace klee
{
class Solver;
class ExprRule;
class Expr;
class Array;
class KnockOut;
// first = symbolic expr, second = old constant
typedef std::pair<ref<Expr>, ref<ConstantExpr> >	replvar_t;

class KnockoutRule
{
public:
	KnockoutRule(const ExprRule* _er, ref<Array>& ko_arr);
	virtual ~KnockoutRule(void);
	bool knockedOut(void) const { return ko.isNull() == false; }
	ref<Expr> getKOExpr(void) const;
	const ExprRule* getExprRule(void) const { return er; }

	ExprRule* createFullRule(Solver* s) const;
	ExprRule* createPartialRule(Solver* s) const;
	ExprRule* createSubtreeRule(Solver* s) const;

	exprtags_ty getTags(int slot = -1 /* all tags */) const;
private:
	bool isConstInvariant(Solver* s) const;

	ref<Expr> trySlot(Solver* s, const ref<Expr>& e_from,
		const replvar_t& rv, int i) const;
	bool findRuleRange(
		Solver* s,
		int& slot,
		ref<Expr>& e_range) const;

	const ExprRule	*er;
	mutable ref<Array>	arr;
	ref<Expr>	ko;
	KnockOut	*kout;
};
}

#endif

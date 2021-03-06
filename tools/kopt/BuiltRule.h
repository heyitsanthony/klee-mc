#ifndef BUILTRULE_H
#define BUILTRULE_H

#include <iostream>
#include "klee/Expr.h"

namespace klee
{
class ExprRule;
class BuiltRule
{
public:
	BuiltRule(
		ExprBuilder	*base_builder,
		ExprBuilder	*new_builder,
		const ExprRule	*er);
	virtual ~BuiltRule() {}

	bool builtAsExpected(void) const { return to_expected == to_actual; }
	bool isIneffective(void) const
	{ return to_actual == from; }

	/* better than from-exp */
	bool isReduced(void) const;

	/* better than expected to-expr */
	bool isBetter(void) const;

	/* worse than expected from-expr */
	bool isWorse(void) const;

	/* does the translated expr have fewer bits than the
	 * expected dest expr? */
	bool fewerBits(void) const;

	void dump(std::ostream& os) const;

	ref<Expr> getFrom(void) const { return from; }

	/* what we expect from to translate to */
	ref<Expr> getToExpected(void) const { return to_expected; }

	/* what from actually translated to */
	ref<Expr> getToActual(void) const { return to_actual; }
private:
	ref<Expr>	from, to_expected, to_actual;
	const ExprRule	*er;
};
}

#endif

#include "../../lib/Expr/ExprRule.h"
#include "klee/util/ExprUtil.h"
#include "BuiltRule.h"

using namespace klee;

BuiltRule::BuiltRule(
	ExprBuilder	*base_builder,
	ExprBuilder	*new_builder,
	const ExprRule	*_er)
: er(_er)
{
	ExprBuilder	*init_eb(Expr::getBuilder());

	Expr::setBuilder(base_builder);
	from = er->getCleanFromExpr();
	to_expected = er->getToExpr();
	
	Expr::setBuilder(new_builder);
	to_actual = er->getCleanFromExpr();

	Expr::setBuilder(init_eb);
}

bool BuiltRule::isReduced(void) const
{ return ExprUtil::getNumNodes(to_actual) < ExprUtil::getNumNodes(from); }

bool BuiltRule::isBetter(void) const
{
	return	ExprUtil::getNumNodes(to_actual) <
		ExprUtil::getNumNodes(to_expected);
}

bool BuiltRule::isWorse(void) const
{
	return	ExprUtil::getNumNodes(to_actual) >
		ExprUtil::getNumNodes(to_expected);
}

void BuiltRule::dump(std::ostream& os) const
{
	os << "FROM-EXPR-EB=" << from << '\n';
	os << "FROM-EXPR-RB=" << to_actual << '\n';
	os << "FROM-RAW-EXPR=" << er->getFromExpr() << '\n';
	os << "TO-EXPR=" << to_expected << '\n';
}

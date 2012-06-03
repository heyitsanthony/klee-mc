#include "klee/Expr.h"
#include "KnockoutRule.h"
#include "KnockoutClass.h"

using namespace klee;

KnockoutClass::KnockoutClass(const KnockoutRule* _kr)
: kr(_kr)
{}

void KnockoutClass::addRule(const KnockoutRule* er)
{
	assert (er->getKOExpr() == kr->getKOExpr() && "WRONG KR CLASS");
	rules.push_back(er);
}

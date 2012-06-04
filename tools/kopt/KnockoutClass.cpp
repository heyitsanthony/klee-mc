#include "static/Sugar.h"
#include "klee/Expr.h"
#include "KnockoutRule.h"
#include "KnockoutClass.h"

using namespace klee;

/* XXX get tags from KO rule-- infer sets on rules */
KnockoutClass::KnockoutClass(const KnockoutRule* _kr)
: root_kr(_kr)
{
	addRule(root_kr);
}

void KnockoutClass::addRule(const KnockoutRule* kr)
{
	ref<Expr>	ko_e;
	exprtags_ty	tags;

	ko_e = kr->getKOExpr();
	assert (ko_e == root_kr->getKOExpr() && "WRONG KR CLASS");
	rules.push_back(kr);

	foreach (it, tags.begin(), tags.end()) {
		ref<Expr>	e;
		e = ExprGetTag::getExpr(ko_e, *it);
		assert (e.isNull() == false);
		assert (e->getKind() == Expr::Constant);
		tagvals[*it].insert(cast<ConstantExpr>(e)->getZExtValue());
	}
}

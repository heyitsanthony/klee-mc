#include "static/Sugar.h"
#include "klee/Expr.h"
#include "../../lib/Expr/ExprRule.h"
#include "KnockoutRule.h"
#include "KnockoutClass.h"
#include <iostream>

using namespace klee;

/* XXX get tags from KO rule-- infer sets on rules */
KnockoutClass::KnockoutClass(const KnockoutRule* _kr)
: root_kr(_kr)
{
	addRule(root_kr);
}

void KnockoutClass::addRule(const KnockoutRule* kr)
{
	ref<Expr>	ko_e, er_e;
	exprtags_ty	tags;

	ko_e = kr->getKOExpr();
	assert (ko_e == root_kr->getKOExpr() && "WRONG KR CLASS");
	rules.push_back(kr);

	tags = kr->getTags();
	if (tags.empty())
		return;

	er_e = kr->getExprRule()->getFromExpr();
	for (const auto &tag : tags) {
		ref<Expr>	e;
		e = ExprGetTag::getExpr(er_e, tag, true, true);
		assert (e.isNull() == false);
		assert (e->getKind() == Expr::Constant);
		tagvals[tag].insert(cast<ConstantExpr>(e)->getZExtValue());
	}
}

ExprRule* KnockoutClass::createRule(Solver* s) const
{
	ExprRule				*ret;
	static unsigned i = 0;
	std::vector<std::pair<int, int> >	sorted_tags;

	i++;
// /* XXX DEBUG */
//	if (root_kr->getExprRule()->getFromExpr()->getKind() != Expr::Ult)
//		return NULL;

	std::cerr << "[KnockoutClass] [" << i << "]: ";

	std::cerr << "RULE OF INTEREST: ";
	root_kr->getExprRule()->print(std::cerr);
	std::cerr << root_kr->getExprRule()->getFromExpr() << '\n';
	std::cerr << '\n';


	if (!root_kr->knockedOut())
		return NULL;

	if ((ret = root_kr->createFullRule(s)) != NULL)
		return ret;

	/* subtree knockouts come first so that dummy constants
	 * will be translated into dummy slots */
#if 1
	if ((ret = root_kr->createSubtreeRule(s)) != NULL) {
		std::cerr << "=====================================\n";
		std::cerr << "GOT SUBTREE RULE:\n";
		std::cerr << "OLD RULE:\n";
		root_kr->getExprRule()->print(std::cerr);

		if (!ret->materialize().isNull()) {
			std::cerr << "V-RULE:\n";
			ret->print(std::cerr);
			std::cerr << "=====================================\n";
			return ret;
		}
		std::cerr << "COULD NOT MATERIALIZE!!\n";
		delete ret;
		ret = NULL;
	}
#endif
#if 1
	foreach (it, tagvals.begin(), tagvals.end()) {
		std::cerr << "TAG: " << it->first
			<< ". SIZE=" << it->second.size() << '\n';
	}

	if ((ret = root_kr->createPartialRule(s)) != NULL)
		return ret;
#endif
	return NULL;
}

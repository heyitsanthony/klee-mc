#include <iostream>

#include "../../lib/Expr/ExprRule.h"
#include "../../lib/Expr/RuleBuilder.h"
#include "klee/Expr.h"
#include "klee/ExprBuilder.h"
#include "static/Sugar.h"
#include "KnockoutRule.h"
#include "KnockoutClass.h"

#include "DBScan.h"

using namespace klee;

extern ExprBuilder::BuilderKind	BuilderKind;

DBScan::DBScan(Solver* _s)
: s(_s)
, uninteresting_c(0)
{
	arr = Array::create("ko_arr", 4096);
	rb = new RuleBuilder(ExprBuilder::create(BuilderKind));
}

DBScan::~DBScan()
{
	if (kc_map.size()) {
		foreach (it, kc_map.begin(), kc_map.end())
			delete it->second;
		kc_map.clear();
	}

	if (kr_list.size()) {
		foreach (it, kr_list.begin(), kr_list.end())
			delete (*it);
		kr_list.clear();
	}


	delete rb;
}

void DBScan::addRule(const ExprRule* er)
{
	KnockoutClass			*kc;
	KnockoutRule			*kr;
	kcmap_ty::const_iterator	kc_it;

	/* knock out what we can */
	kr = new KnockoutRule(er, arr.get());
	if (kr->knockedOut() == false) {
		/* nothing changed-- not interesting */
		uninteresting_c++;
		delete kr;
		return;
	}

	kr_list.push_back(kr);
	
	/* classify knockout rule */
	kc_it = kc_map.find(kr->getKOExpr());
	if (kc_it == kc_map.end()) {
		kc = new KnockoutClass(kr);
		kc_map.insert(std::make_pair(kr->getKOExpr(), kc));
	} else {
		kc = kc_it->second;
		kc->addRule(kr);
	}
}

void DBScan::loadKnockoutRulesFromBuilder()
{
	assert (kc_map.size() == 0 && "Already loaded KO rules");
		
	foreach (it, rb->begin(), rb->end())
		addRule(*it);

	std::cerr << "[KO] Uninteresting: " << uninteresting_c << '\n';
}

void DBScan::histo(void)
{
	std::map<
		unsigned /* # rules in class */,
		unsigned /* # classes */ >	ko_c;

	loadKnockoutRulesFromBuilder();

	foreach (it, kc_map.begin(), kc_map.end()) {
		const KnockoutRule	*kr;
		unsigned		rule_c;

		kr = it->second->front();
		rule_c = it->second->size();
		ko_c[rule_c] = ko_c[rule_c] + 1;
	}

	std::cout << "# RULES-IN-CLASS | TOTAL-CLASS-RULES" << '\n';
	unsigned total = 0;
	foreach (it, ko_c.begin(), ko_c.end()) {
		total += it->first * it->second;
	}

	unsigned cur_total = 0;
	foreach (it, ko_c.begin(), ko_c.end()) {
		cur_total += it->first * it->second;
		std::cout << it->first << ' ' << 
			(100*cur_total)/total << '\n';
	}
}

void DBScan::punchout(std::ostream& os)
{
	std::vector<newrule_ty>		valid_kos;
	unsigned			rule_match_c;

	loadKnockoutRulesFromBuilder();

	rule_match_c = 0;
	foreach (it, kc_map.begin(), kc_map.end()) {
		const KnockoutClass	*kc;
		const KnockoutRule	*kr;
		ExprRule		*new_rule;

		kc = it->second;

		/* don't try anything with unique rules */
		if (kc->size() < 2)
			continue;

		kr = kc->front();
		new_rule = kr->createRule(s);
		if (new_rule == NULL)
			continue;

		valid_kos.push_back(std::make_pair(kr, new_rule));
		rule_match_c += kc->size();
		std::cerr << "TOTAL RULES: " << valid_kos.size() << '\n';
		new_rule->printBinaryRule(os);
		os.flush();
	}
	std::cerr << '\n';
	std::cerr << "TOTAL VALID KO's: " << valid_kos.size() << '\n';
	std::cerr << "TOTAL RULES MATCHED: " << rule_match_c << '\n';

	/* free created rules */
	foreach (it, valid_kos.begin(), valid_kos.end())
		delete it->second;
}

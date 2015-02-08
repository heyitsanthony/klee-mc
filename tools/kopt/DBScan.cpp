#include <iostream>

#include <llvm/Support/CommandLine.h>
#include "../../lib/Expr/ExprRule.h"
#include "../../lib/Expr/RuleBuilder.h"
#include "klee/Expr.h"
#include "klee/ExprBuilder.h"
#include "static/Sugar.h"
#include "KnockoutRule.h"
#include "KnockoutClass.h"

#include "DBScan.h"

using namespace klee;
using namespace llvm;

namespace llvm
{
	cl::opt<std::string>
	UninterestingFile(
		"uninteresting-file",
		cl::desc("File to write uninteresting rules."),
		cl::init(""));

	cl::opt<std::string>
	UniqueFile(
		"unique-file",
		cl::desc("File to write unique knockout rules."),
		cl::init(""));

	cl::opt<std::string>
	StubbornFile(
		"stubborn-file",
		cl::desc("File to write rules that weren't generalized."),
		cl::init(""));
}

extern ExprBuilder::BuilderKind	BuilderKind;

DBScan::DBScan(Solver* _s)
: s(_s)
{
	arr = Array::create("ko_arr", 4096);
	rb = RuleBuilder::create(ExprBuilder::create(BuilderKind));
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
	kr = new KnockoutRule(er, arr);
	if (kr->knockedOut() == false) {
		/* nothing changed-- not interesting */
		uninteresting.push_back(er);
		return;
	}

	kr_list.push_back(kr);
	
	/* classify knockout rule */
	kc_it = kc_map.find(kr->getKOExpr());
	if (kc_it == kc_map.end()) {
		kc = new KnockoutClass(kr);
		kc_map.insert(std::make_pair(kr->getKOExpr(), kc));
		return;
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

	std::cerr << "[KO] Uninteresting: " << uninteresting.size() << '\n';
}

void DBScan::histo(void)
{
	std::map<
		unsigned /* # rules in class */,
		unsigned /* # classes */ >	ko_c;

	loadKnockoutRulesFromBuilder();

	for (auto &p : kc_map) {
		unsigned rule_c = p.second->size();
		ko_c[rule_c] = ko_c[rule_c] + 1;
	}

	std::cout << "# RULES-IN-CLASS | TOTAL-CLASS-RULES" << '\n';
	unsigned total = 0;
	for (auto &p : ko_c)
		total += p.first * p.second;
	assert (total && "No rules given?");

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
	std::vector<const ExprRule*>	unique_rules;
	std::vector<const ExprRule*>	stubborn_rules;
	unsigned			rule_match_c;

	loadKnockoutRulesFromBuilder();

	rule_match_c = 0;
	foreach (it, kc_map.begin(), kc_map.end()) {
		const KnockoutClass	*kc;
		ExprRule		*new_rule;
		const ExprRule		*old_rule_example;

		kc = it->second;

		/* don't try generalizing unique rules */
		old_rule_example = kc->front()->getExprRule();
		if (kc->size() < 2 && old_rule_example->isNaive()) {
			unique_rules.push_back(old_rule_example);
			continue;
		}

		new_rule = kc->createRule(s);
		if (new_rule == NULL) {
			/* could not generalize; save all rules */
			foreach (it2, kc->begin(), kc->end()) {
				const ExprRule	*er;
				er = (*it2)->getExprRule();
				stubborn_rules.push_back(er);
			}
			continue;
		}

		valid_kos.push_back(std::make_pair(kc, new_rule));
		rule_match_c += kc->size();
		std::cerr << "=========================================\n";
		std::cerr << "[DBScan] TOTAL RULES: " << valid_kos.size() << '\n';
		std::cerr << "=========================================\n";
		new_rule->printBinaryRule(os);
		os.flush();
	}
	std::cerr << '\n';
	std::cerr << "TOTAL VALID KO's: " << valid_kos.size() << '\n';
	std::cerr << "TOTAL RULES MATCHED: " << rule_match_c << '\n';

	/* free created rules */
	foreach (it, valid_kos.begin(), valid_kos.end())
		delete it->second;

	saveRules(UniqueFile, unique_rules);
	saveRules(UninterestingFile, uninteresting);
	saveRules(StubbornFile, stubborn_rules);
}

void DBScan::saveRules(
	const std::string& fname, const std::vector<const ExprRule*>& ers)
{
	if (fname.empty())
		return;

	std::ofstream	ofs(fname.c_str());
	foreach (it, ers.begin(), ers.end())
		(*it)->printBinaryRule(ofs);
}

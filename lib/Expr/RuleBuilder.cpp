#include <sys/types.h>
#include <dirent.h>
#include <llvm/Support/CommandLine.h>
#include <algorithm>
#include <sstream>
#include <iostream>

#include "static/Sugar.h"
#include "ExprRule.h"
#include "RuleBuilder.h"

using namespace klee;
using namespace llvm;

namespace {
	cl::opt<std::string>
	RuleDir(
		"rule-dir",
		cl::desc("Directory containing rules for RuleBuilder"),
		cl::init("rules"));
}

uint64_t RuleBuilder::hit_c = 0;
uint64_t RuleBuilder::miss_c = 0;
uint64_t RuleBuilder::rule_miss_c = 0;

RuleBuilder::RuleBuilder(ExprBuilder* base)
: eb(base), depth(0)
{
	loadRules(RuleDir.c_str());
}

RuleBuilder::~RuleBuilder()
{
	foreach (it, rules.begin(), rules.end())
		delete (*it);
	rules.clear();
	delete eb;
}

static bool rulesSort(const ExprRule* l, const ExprRule* r)
{
	if (l->getToNodeCount() == r->getToNodeCount())
		return l->getFromNodeCount() < r->getFromNodeCount();
	return l->getToNodeCount() < r->getToNodeCount();
}

void RuleBuilder::loadRules(const char* ruledir)
{
	struct dirent	*de;
	DIR		*d;

	d = opendir(ruledir);
	assert (d != NULL && "Could not open rules directory!");

	while ((de = readdir(d))) {
		ExprRule		*er;
		std::stringstream	s;

		s << ruledir << "/" << de->d_name;
		er = ExprRule::loadPrettyRule(s.str().c_str());
		if (er == NULL)
			continue;

		rules.push_back(er);
	}

	std::sort(rules.begin(), rules.end(), rulesSort);

	closedir(d);
}

ref<Expr> RuleBuilder::tryApplyRules(const ref<Expr>& in)
{
	ref<Expr>	ret;

	if (in->getKind() == Expr::Constant)
		return in;

	assert (depth == 0);

	/* don't call back to self in case we find an optimization! */
	depth = 1;

	ret = in;
	/* apply every rule until something sticks
	 * TODO: trie of pattern matching rules */
	foreach (it, rules.begin(), rules.end()) {
		ExprRule	*er = *it;
		ref<Expr>	new_expr;

		new_expr = er->apply(in);
		if (!new_expr.isNull()) {
			hit_c++;
			ret = new_expr;
			break;
		}

		rule_miss_c++;
	}
	depth = 0;

	if ((void*)ret.get() == (void*)in.get())
		miss_c++;

	return ret;
}

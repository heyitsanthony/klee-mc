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

	cl::opt<bool>
	ApplyAllRules(
		"try-all-rules",
		cl::desc("Iterate through all rules until a match is found."),
		cl::init(false));

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
	foreach (it, rules_arr.begin(), rules_arr.end())
		delete (*it);
	rules_arr.clear();
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

		rules_arr.push_back(er);
		rules_trie.add(er->getFromKey(), er);
	}

	std::sort(rules_arr.begin(), rules_arr.end(), rulesSort);

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

	if (ApplyAllRules)
		ret = tryAllRules(in);
	else
		ret = tryTrieRules(in);

	depth = 0;

	if ((void*)ret.get() == (void*)in.get())
		miss_c++;
	else
		hit_c++;

	return ret;
}

class TrieRuleIterator : public ExprRule::RuleIterator
{
public:
	TrieRuleIterator(const RuleBuilder::ruletrie_ty& _rt)
	: rt(_rt), found_rule(NULL), last_depth(0), label_depth(0)
	{}

	virtual bool isDone(void) const { return found_rule != NULL; }
	virtual void reset(void)
	{
		found_rule = NULL;
		last_depth = 0;
		label_depth = 0;
		it = rt.begin();
	}

	virtual bool matchValue(uint64_t v)
	{
		bool	is_matched;

		if (found_rule)
			return false;

		it.next(v);
		if (it.isFound())
			found_rule = it.get();

		is_matched = (it != rt.end() || found_rule);
		return is_matched;
	}
	virtual bool matchLabel(uint64_t& v)
	{
		bool		found_label;
		uint64_t	target_label;

		if (found_rule)
			return false;


		if (last_label_v.size() <= label_depth) {
			last_label_v.push_back(OP_LABEL_MASK);
			target_label = OP_LABEL_MASK;
		} else
			target_label = last_label_v[label_depth];

		if (target_label == ~0ULL) {
			/* already know not to look for anything here */
			label_depth++;
			return false;
		}

		found_label = it.tryNextMin(target_label, v);

		last_label_v[label_depth] = (found_label) ? v+1 : ~0ULL;
		if (last_label_v[label_depth] == ~0ULL) {
			/* failed to find label; invalidate / trunc */
			last_label_v.resize(label_depth+1);
		}

		label_depth++;

		if (it.isFound())
			found_rule = it.get();

		return found_label;
	}
	virtual const ExprRule::flatrule_ty& getToRule(void) const
	{
		assert (found_rule != NULL);
		return found_rule->getToKey();
	}

	bool bumpSlot(void)
	{
		int i;
		for (i = last_label_v.size() - 1; i >= 0; i--) {
			if (last_label_v[i] == ~0ULL)
				continue;

			last_label_v.resize(i+1);
			last_label_v[i]++;
			return true;
		}
		return false;
	}

	virtual ~TrieRuleIterator() {}

private:
	/* we use this to choose whether to seek out a label or not */
	const RuleBuilder::ruletrie_ty	&rt;
	ExprRule			*found_rule;
	std::vector<uint64_t>		last_label_v;
	unsigned			last_depth;
	unsigned			label_depth;
	RuleBuilder::ruletrie_ty::const_iterator	it;
};

ref<Expr> RuleBuilder::tryTrieRules(const ref<Expr>& in)
{
	TrieRuleIterator	tri(rules_trie);
	ref<Expr>		new_expr(0);

	while (1) {
		new_expr = ExprRule::apply(in, tri);
		if (new_expr.isNull() == false)
			return new_expr;

		rule_miss_c++;
		if (tri.bumpSlot() == false)
			break;
	}

	return in;
}

ref<Expr> RuleBuilder::tryAllRules(const ref<Expr>& in)
{
	foreach (it, rules_arr.begin(), rules_arr.end()) {
		ExprRule	*er = *it;
		ref<Expr>	new_expr;

		new_expr = er->apply(in);
		if (!new_expr.isNull())
			return new_expr;

		rule_miss_c++;
	}

	return in;
}



#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <llvm/Support/CommandLine.h>
#include <algorithm>
#include <sstream>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include "../Solver/SMTPrinter.h"
#include "klee/Solver.h"
#include "klee/util/ExprUtil.h"
#include "klee/Internal/ADT/Hash.h"

#include "static/Sugar.h"
#include "ExprRule.h"
#include "RuleBuilder.h"

using namespace klee;
using namespace llvm;

namespace {
	cl::opt<std::string>
	DumpUsedRules(
		"dump-used-rules",
		cl::desc("Write used rules to a file."),
		cl::init(""));

	cl::opt<bool>
	RBMissFilter(
		"rb-miss-filter",
		cl::desc("Filter out misses by hash"),
		cl::init(true));

	cl::opt<std::string>
	RuleDir(
		"rule-dir",
		cl::desc("Directory containing rules for RuleBuilder"),
		cl::init("rules"));

	cl::opt<std::string>
	RuleDBFile(
		"rule-file",
		cl::desc("Rule database file"),
		cl::init("brule.db"));

	cl::opt<bool>
	ApplyAllRules(
		"try-all-rules",
		cl::desc("Iterate through all rules until a match is found."),
		cl::init(false));

	cl::opt<bool> ShowXlate("show-xlated", cl::init(false));

	cl::opt<bool> DumpRuleMiss("dump-rule-miss", cl::init(false));
}

uint64_t RuleBuilder::hit_c = 0;
uint64_t RuleBuilder::miss_c = 0;
uint64_t RuleBuilder::rule_miss_c = 0;
uint64_t RuleBuilder::miss_filtered_c = 0;
uint64_t RuleBuilder::filter_size = 0;
const ExprRule* RuleBuilder::last_er = NULL;

std::set<const ExprRule*> RuleBuilder::rules_used;

RuleBuilder::RuleBuilder(ExprBuilder* base)
: eb(base), depth(0), recur(0), rule_ofs(0)
{
	loadRules();
	if (DumpRuleMiss)
		mkdir("miss_dump", 0777);
	if (DumpUsedRules.size() != 0)
		rule_ofs = new std::ofstream(DumpUsedRules.c_str());
}

RuleBuilder::~RuleBuilder()
{
	if (rule_ofs) delete rule_ofs;

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

void RuleBuilder::loadRules()
{
	bool			ok;
	const std::string*	load_s;

	load_s = &getDBPath();
	ok = loadRuleDB(getDBPath().c_str());
	if (!ok) {
		load_s = &RuleDir;
		ok = loadRuleDir(RuleDir.c_str());
	}

	if (!ok) {
		std::cerr << "[RuleBuilder] Could not load rules\n";
		return;
	}

	std::cerr
		<< "[RuleBuilder] Loaded " << rules_arr.size()
		<< " rules from " << *load_s << ".\n";
}

void RuleBuilder::eraseDBRule(rulearr_ty::const_iterator& it)
{
	const ExprRule	*to_rmv;
	std::fstream	ifs(	getDBPath().c_str(),
				std::ios_base::in |
				std::ios_base::out |
				std::ios_base::binary);
	std::streampos	last_pos;

	if (!ifs.good() || ifs.bad() || ifs.fail() || ifs.eof()) {
		assert (0 == 1 && "Could not update erasedb??");
		return;
	}

	to_rmv = *it;
	/* first, try the hint */
	if (to_rmv->getOffsetHint())
		if (eraseDBRuleHint(ifs, to_rmv))
			return;

	/* hint was bogus-- do a full scan like a jackass */
	last_pos = 0;
	while (ifs.eof() == false) {
		if (tryEraseDBRule(ifs, last_pos, to_rmv))
			return;
	}
}

bool RuleBuilder::tryEraseDBRule(
	std::fstream& ifs,
	std::streampos& last_pos,
	const ExprRule* to_rmv)
{
	ExprRule	*er;
	std::streampos	new_pos;

	er = ExprRule::loadBinaryRule(ifs);
	new_pos = ifs.tellg();

	if (er == NULL) {
		last_pos = new_pos;
		return false;
	}

	if (*er == *to_rmv) {
		assert (last_pos < new_pos);
		ifs.seekg(last_pos);
		ExprRule::printTombstone(ifs, new_pos - last_pos);
		delete er;
		return true;
	}

	last_pos = new_pos;
	delete er;
	return false;
}

bool RuleBuilder::eraseDBRuleHint(
	std::fstream& ifs,
	const ExprRule* to_rmv)
{
	ExprRule	*er;
	std::streampos	off_hint;

	off_hint = (std::streampos)to_rmv->getOffsetHint();

	ifs.seekg(off_hint);
	er = ExprRule::loadBinaryRule(ifs);
	if (er != NULL && *er == *to_rmv) {
		std::streampos new_pos = ifs.tellg();
		ifs.seekg(off_hint);
		ExprRule::printTombstone(ifs, new_pos - off_hint);
		delete er;
		return true;

	}

	if (er != NULL) delete er;

	ifs.seekg(0);
	return false;
}

const std::string& RuleBuilder::getDBPath(void) const { return RuleDBFile; }

bool RuleBuilder::loadRuleDB(const char* ruledir)
{
	std::ifstream	ifs(ruledir);

	if (!ifs.good() || ifs.bad() || ifs.fail() || ifs.eof())
		return false;

	while (ifs.eof() == false)
		addRule(ExprRule::loadBinaryRule(ifs));

	return true;
}

void RuleBuilder::addRule(ExprRule* er)
{
	if (er == NULL)
		return;

	rules_arr.push_back(er);
	rules_trie.add(er->getFromPattern().stripConstExamples(), er);
}

bool RuleBuilder::loadRuleDir(const char* ruledir)
{
	struct dirent	*de;
	DIR		*d;

	d = opendir(ruledir);
	if (d == NULL)
		return false;

	while ((de = readdir(d))) {
		std::stringstream	s;
		s << ruledir << "/" << de->d_name;
		addRule(ExprRule::loadPrettyRule(s.str().c_str()));
	}

	std::sort(rules_arr.begin(), rules_arr.end(), rulesSort);

	closedir(d);
	return true;
}

#define RB_MAX_RECUR	5

ref<Expr> RuleBuilder::tryApplyRules(const ref<Expr>& in)
{
	ref<Expr>	ret;
	unsigned	old_depth;

	if (in->getKind() == Expr::Constant)
		return in;

	if (recur > RB_MAX_RECUR)
		return in;

	if (RBMissFilter) {
		/* known to miss? why bother.. */
		if (miss_filter.find(in->hash()) != miss_filter.end()) {
			miss_filtered_c++;
			return in;
		}
	}

	recur++;

	/* don't call back to self in case we find an optimization! */
	old_depth = depth;
	depth = 1;

	if (ApplyAllRules)
		ret = tryAllRules(in);
	else
		ret = tryTrieRules(in);

	recur--;
	depth = old_depth;

	if ((void*)ret.get() == (void*)in.get()) {
		miss_c++;
		if (DumpRuleMiss) {
			SMTPrinter::dump(Query(ret), "miss_dump/miss");
		}
		if (RBMissFilter) {
			miss_filter.insert(in->hash());
			filter_size++;
		}
	} else {
		if (ShowXlate) {
			std::cerr << "[RuleBuilder] XLated!\n";
			std::cerr << in << '\n';
			std::cerr << "->" << '\n';
			std::cerr << ret << '\n';
		}
		hit_c++;
	}

	return ret;
}

class TrieRuleIterator : public ExprPatternMatch::RuleIterator
{
public:
	TrieRuleIterator(const RuleBuilder::ruletrie_ty& _rt)
	: rt(_rt), found_rule(NULL), label_depth(0)
	{}

	virtual bool isDone(void) const { return found_rule != NULL; }
	virtual void reset(void)
	{
		found_rule = NULL;
		label_depth = 0;
		it = rt.begin();
	}

	virtual bool skipValue(void)
	{
		/* skip value only makes sense if there's an example constant.
		 * we strip the example constants out; so this is a no-op */
		return true;
	}

	virtual bool matchValue(uint64_t v)
	{
		bool	is_matched;

		if (isDone()) return false;

		it.next(v);
		if (it.isFound())
			found_rule = it.get();

		is_matched = (it != rt.end() || found_rule);
		return is_matched;
	}

	virtual bool matchLabel(uint64_t& v);
	virtual bool matchLabel(uint64_t& v, uint64_t mask);

	void dump(void) { it.dump(std::cerr); }

	/* Find last non-invalid label and increment by one,
	 * then shrink label list to that label to reflect unevaluated
	 * labels. Essentially simulates DFS search. */
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

	void dumpSlots(std::ostream& os)
	{
		os << "Dumping slots:\n";
		foreach (it, last_label_v.begin(), last_label_v.end())
			os << (void*)(*it) << ' ';
		os << '\n';
	}

	virtual ~TrieRuleIterator() {}
	virtual ExprRule* getExprRule(void) const { return found_rule; }
private:
	/* we use this to choose whether to seek out a label or not */
	const RuleBuilder::ruletrie_ty	&rt;
	ExprRule			*found_rule;
	std::vector<uint64_t>		last_label_v;
	unsigned			label_depth;
	RuleBuilder::ruletrie_ty::const_iterator	it;
};

bool TrieRuleIterator::matchLabel(uint64_t& v)
{
	if (found_rule)
		return false;

	return matchLabel(v, OP_LABEL_MASK);
}

bool TrieRuleIterator::matchLabel(uint64_t& v, uint64_t mask)
{
	bool		found_label;
	uint64_t	target_label;

	if (last_label_v.size() <= label_depth) {
		/* try new label */
		last_label_v.push_back(mask);
		target_label = mask;
	} else {
		/* replay label from last run */
		target_label = last_label_v[label_depth];
	}

	if (target_label == ~0ULL) {
		/* already know not to look for anything here */
		label_depth++;
		return false;
	}

	/* find minimum where label >= target_label */
	found_label = it.tryNextMin(target_label, v);

	/* mark bump replay label */
	last_label_v[label_depth] = (found_label) ? v : ~0ULL;
	if (last_label_v[label_depth] == ~0ULL) {
		/* failed to find label; invalidate / trunc */
		last_label_v.resize(label_depth+1);
	}

	label_depth++;

	if (it.isFound()) {
		found_rule = it.get();
	}
	
	return found_label;
}

ref<Expr> RuleBuilder::tryTrieRules(const ref<Expr>& in)
{
	TrieRuleIterator	tri(rules_trie);
	ref<Expr>		new_expr(0);

	while (1) {
		new_expr = ExprRule::apply(in, tri);
		if (new_expr.isNull() == false) {
			bool	inserted;

			last_er = tri.getExprRule();
			inserted = rules_used.insert(last_er).second;
			if (inserted && rule_ofs != NULL)
				last_er->printBinaryRule(*rule_ofs);
			return new_expr;
		}

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
		if (!new_expr.isNull()) {
			last_er = er;
			return new_expr;
		}

		rule_miss_c++;
	}

	return in;
}

bool RuleBuilder::hasRule(const char* fname)
{
	FILE			*f;
	std::string		rule_file;
	unsigned char		buf[4096];
	unsigned		sz;
	struct stat		s;

	/* expert quality */
	f = fopen(fname, "rb");
	if (f == NULL) {
		std::cerr << "Could not load new rule '" << fname << "'\n";
		return false;
	}
	sz = fread(buf, 1, 4095, f);
	assert (sz > 0 && "DID NOT GET ANY DATA??");
	buf[sz] = '\0';
	fclose(f);

	if (sz == 0)
		return false;

	rule_file = RuleDir + "/" + Hash::MD5(buf, sz);
	if (stat(rule_file.c_str(), &s) == 0) {
		std::cerr << "[RuileBuilder] Already has " << rule_file << '\n';
		return true;
	}

	return false;
}

#include <sys/types.h>
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
std::set<ExprRule*> RuleBuilder::rules_used;

RuleBuilder::RuleBuilder(ExprBuilder* base)
: eb(base), depth(0), recur(0)
{
	loadRules();
	if (DumpRuleMiss)
		mkdir("miss_dump", 0777);
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
	last_pos = 0;
	while (ifs.eof() == false) {
		ExprRule	*er;
		std::streampos	new_pos;

		er = ExprRule::loadBinaryRule(ifs);
		new_pos = ifs.tellg();

		if (er == NULL) {
			last_pos = new_pos;
			continue;
		}

		if (*er == *to_rmv) {
			assert (last_pos < new_pos);
			ifs.seekg(last_pos);
			ExprRule::printTombstone(ifs, new_pos - last_pos);
			delete er;
			break;
		}

		last_pos = new_pos;
		delete er;
	}
}

const std::string& RuleBuilder::getDBPath(void) const { return RuleDBFile; }

bool RuleBuilder::loadRuleDB(const char* ruledir)
{
	std::ifstream	ifs(ruledir);

	if (!ifs.good() || ifs.bad() || ifs.fail() || ifs.eof())
		return false;

	while (ifs.eof() == false) {
		ExprRule	*er;

		er = ExprRule::loadBinaryRule(ifs);
		if (er == NULL)
			continue;

		rules_arr.push_back(er);
		rules_trie.add(er->getFromKey(), er);
	}

	return true;
}

bool RuleBuilder::loadRuleDir(const char* ruledir)
{
	struct dirent	*de;
	DIR		*d;

	d = opendir(ruledir);
	if (d == NULL)
		return false;

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
	return true;
}


ref<Expr> RuleBuilder::tryApplyRules(const ref<Expr>& in)
{
	ref<Expr>	ret;
	unsigned	old_depth;

	if (in->getKind() == Expr::Constant)
		return in;

	recur++;
	if (recur > 10) {
		recur--;
		return in;
	}

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
	virtual ExprRule* getExprRule(void) const { return found_rule; }
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
		if (new_expr.isNull() == false) {
			rules_used.insert(tri.getExprRule());
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
		if (!new_expr.isNull())
			return new_expr;

		rule_miss_c++;
	}

	return in;
}

#include "openssl/md5.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
static const char ugh[] = "0123456789abcdef";
bool RuleBuilder::hasRule(const char* fname)
{
	FILE			*f;
	char			buf[4096];
	unsigned		sz;
	std::string		rule_file;
	struct stat		s;
	unsigned char		md[16];

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

	MD5((unsigned char*)buf, sz, md);

	std::stringstream ss;
	for (unsigned i = 0; i < 16; i++)
		ss	<< ugh[(md[i] & 0xf0) >> 4]
			<< ugh[md[i] & 0x0f];

	rule_file = RuleDir + "/" + ss.str();
	if (stat(rule_file.c_str(), &s) == 0) {
		std::cerr << "[RuileBuilder] Already has " << rule_file << '\n';
		return true;
	}

	return false;
}

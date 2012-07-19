#include <iostream>
#include <sstream>

#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/system_error.h>

#include "../../lib/Expr/SMTParser.h"
#include "../../lib/Expr/ExprRule.h"
#include "../../lib/Expr/RuleBuilder.h"
#include "../../lib/Expr/OptBuilder.h"
#include "../../lib/Expr/ExtraOptBuilder.h"
#include "../../lib/Core/StateSolver.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>

#include "static/Sugar.h"
#include "klee/ExprBuilder.h"
#include "klee/Solver.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/Assignment.h"

#include "BuiltRule.h"
#include "Benchmarker.h"
#include "DBScan.h"

using namespace llvm;
using namespace klee;
using namespace klee::expr;

ExprBuilder::BuilderKind	BuilderKind;
int				WorkerForks;

#define DEF_OPT(x,y,z) cl::opt<bool> x(y,cl::desc(z),cl::init(false))

namespace llvm
{
	cl::opt<std::string>
	InputFile(
		cl::desc("<equivexpr proof .smt>"),
		cl::Positional,
		cl::init("-"));

	cl::opt<int, true>
	WorkerForksProxy("worker-forks",
		cl::desc("Number of forked workers to use"),
		cl::location(WorkerForks),
		cl::init(0));

	cl::opt<std::string>
	ApplyRule(
		"apply-rule",
		cl::desc("Apply given rule file to input smt"),
		cl::init(""));

	cl::opt<int>
	SplitDB(
		"split-db",
		cl::desc("Split rule data base into n chunks"),
		cl::init(0));

	cl::opt<int>
	ExtractRule(
		"extract-rule",
		cl::desc("Extract rule from brule file."),
		cl::init(-1));

	static cl::opt<ExprBuilder::BuilderKind,true>
	BuilderKindProxy("builder",
		cl::desc("Expression builder:"),
		cl::location(BuilderKind),
		cl::init(ExprBuilder::ExtraOptsBuilder),
		cl::values(
			clEnumValN(ExprBuilder::DefaultBuilder, "default",
			"Default expression construction."),
			clEnumValN(ExprBuilder::ConstantFoldingBuilder,
			"constant-folding",
			"Fold constant expressions."),
			clEnumValN(ExprBuilder::SimplifyingBuilder, "simplify",
			"Fold constants and simplify expressions."),
			clEnumValN(ExprBuilder::HandOptBuilder, "handopt",
			"Hand-optimized builder."),
			clEnumValN(ExprBuilder::ExtraOptsBuilder, "extraopt",
			"Extra Hand-optimized builder."),
			clEnumValEnd));

DEF_OPT(BenchRB, "benchmark-rb", "Benchmark rule builder with random queries");
DEF_OPT(BenchmarkRules, "benchmark-rules", "Benchmark rules with random queries");
DEF_OPT(DBPunchout, "db-punchout", "Punch out constants in DB rules");
DEF_OPT(DBHisto, "db-histo", "Print histogram of DB rule classes");
DEF_OPT(CheckRule, "check-rule", "Check a rule file.");
DEF_OPT(EraseShadows, "erase-shadows", "Erase shadowed rules.");
DEF_OPT(VerifyDB, "verify-db", "Verify rule database works and validates.");
DEF_OPT(CheckDB, "check-db", "Verify that rule database is working");
DEF_OPT(DedupDB, "dedup-db", "Remove duplicates from DB");
DEF_OPT(CheckDup, "check-dup", "Check if duplicate rule");
DEF_OPT(DumpBinRule, "dump-bin", "Dump rule in binary format.");
DEF_OPT(CompareDBs, "compare-db", "Compute % of input which is in rule-file");
DEF_OPT(DumpDB, "dump-db", "Dump rule db in pretty format.");
DEF_OPT(DumpPattern, "dump-pat", "Dump rule db patterns");
DEF_OPT(BRuleXtive, "brule-xtive", "Search for transitive rules in brule database");
DEF_OPT(BRuleRebuild, "brule-rebuild", "Rebuild brule file.");
DEF_OPT(ExtractConstrs, "extract-constrs", "Extract rules with constraints");
DEF_OPT(ExtractFrees, "extract-free", "Extract rules with free vars");
DEF_OPT(AddRule, "add-rule", "Add rule to brule file.");
}

void rebuildBRules(Solver* s, const std::string& InputPath);
extern void xtiveBRule(ExprBuilder *eb, Solver* s);

bool checkRule(const ExprRule* er, Solver* s, std::ostream&);
bool getRuleCex(const ExprRule* er, Solver* s, std::ostream&);
bool getExprCex(
	Solver* s, const ref<Expr>& e1, const ref<Expr>& e2,
	std::ostream& os);


/*
(= (ite (bvult (bvadd bv2936[64] ( zero_extend[56] ( select ?e1 bv37[32] )))
	bv3840[64])
 bv1[1] bv0[1])
 bv0[1]))
*/
static bool getEquivalenceInEq(ref<Expr> e, ref<Expr>& lhs, ref<Expr>& rhs)
{
	const ConstantExpr	*ce;
	const EqExpr		*ee;
	const SelectExpr	*se;

	ee = dyn_cast<EqExpr>(e);
	if (!ee) return false;
	assert (ee && "Expected TLS to be EqExpr!");

	ce = dyn_cast<ConstantExpr>(ee->getKid(1));
	if (!ce) return false;
	assert (ce && ce->getZExtValue() == 0);

	se = dyn_cast<SelectExpr>(ee->getKid(0));
	if (!se) return false;
	assert (se && "Expected (Eq (Sel x) 0)");

	ce = dyn_cast<ConstantExpr>(se->trueExpr);
	if (!ce) return false;
	assert (ce && ce->getZExtValue() == 1);

	ce = dyn_cast<ConstantExpr>(se->falseExpr);
	if (!ce) return false;
	assert (ce && ce->getZExtValue() == 0);

	ee = dyn_cast<EqExpr>(se->cond);
	if (ee) return false;
	assert (!ee && "Expected (Eq (Sel ((NotEq) x y) 1 0) 0)");

	lhs = se->cond;
	rhs = MK_CONST(1, 1);

	return true;
}

static bool getEquivalenceEq(ref<Expr> e, ref<Expr>& lhs, ref<Expr>& rhs)
{
	const ConstantExpr	*ce;
	const EqExpr		*ee;
	const SelectExpr	*se;

	ee = dyn_cast<EqExpr>(e);
	if (!ee) return false;
	assert (ee && "Expected TLS to be EqExpr!");

	ce = dyn_cast<ConstantExpr>(ee->getKid(1));
	if (!ce) return false;
	assert (ce && ce->getZExtValue() == 0);

	se = dyn_cast<SelectExpr>(ee->getKid(0));
	if (!se) return false;
	assert (se && "Expected (Eq (Sel x) 0)");

	ce = dyn_cast<ConstantExpr>(se->trueExpr);
	if (!ce) return false;
	assert (ce && ce->getZExtValue() == 1);

	ce = dyn_cast<ConstantExpr>(se->falseExpr);
	if (!ce) return false;
	assert (ce && ce->getZExtValue() == 0);

	ee = dyn_cast<EqExpr>(se->cond);
	if (!ee) return false;
	assert (ee && "Expected (Eq (Sel (Eq x y) 1 0) 0)");

	lhs = ee->getKid(0);
	rhs = ee->getKid(1);

	return true;
}

static void getEquivalence(ref<Expr> e, ref<Expr>& lhs, ref<Expr>& rhs)
{
	if (getEquivalenceEq(e, lhs, rhs))
		return;

	if (getEquivalenceInEq(e, lhs, rhs))
		return;

	std::cerr << "BAD EQUIV\n";
	exit(-1);
}

static bool checkDup(ExprBuilder* eb, Solver* s)
{
	ExprRule	*er = ExprRule::loadRule(InputFile.c_str());
	RuleBuilder	*rb;
	ExprBuilder	*old_eb;
	ref<Expr>	old_expr, rb_expr;
	bool		ret;

	if (!er) {
		std::cerr << "[kopt] No rule given\n.";
		return false;
	}

	rb = RuleBuilder::create(ExprBuilder::create(BuilderKind));
	old_expr = er->materialize();
	old_eb = Expr::setBuilder(rb);
	rb_expr = er->materialize();
	Expr::setBuilder(old_eb);

	std::cerr 	<< "OLD-EB: " << old_expr << '\n'
			<< "RB-EB: " << rb_expr << '\n';

	if (old_expr != rb_expr) {
		/* rule builder had some kind of effect */
		std::cout << "DUP\n";
		ret = true;
	} else {
		std::cout << "NEW\n";
		ret = false;
	}

	delete rb;
	delete er;

	return ret;
}

bool getExprCex(
	Solver* s, const ref<Expr>& e1, const ref<Expr>& e2,
	std::ostream& os)
{
	ref<Expr>	eq_expr;
	bool		mustBeTrue;

	eq_expr = EqExpr::create(e1, e2);
	if (!s->mustBeTrue(Query(eq_expr), mustBeTrue))
		return true;

	if (mustBeTrue) return true;

	Assignment	a(eq_expr);

	if (!s->getInitialValues(Query(eq_expr), a)) {
		std::cerr << "Could not get initial values\n";
		return false;
	}
	a.print(os);

	return false;
}


bool getRuleCex(const ExprRule* er, Solver* s, std::ostream& os)
{
	bool		ok;

	ok = checkRule(er, s, os);
	if (ok) return true;

	ref<Expr>	re(er->materialize());
	if (re.isNull()) {
		std::cerr << "Bad materialize\n";
		return false;
	}

	Assignment	a(re);

	ok = s->getInitialValues(Query(re), a);
	if (!ok) {
		std::cerr << "Bad getInitV\n";
		return false;
	}

	std::cerr << "RULE-EQ-EXPR: " << re << '\n';
	a.print(std::cerr);
	return false;
}

/* return false if rule did not check out as OK */
bool checkRule(const ExprRule* er, Solver* s, std::ostream& os)
{
	ref<Expr>	rule_expr;
	bool		ok, mustBeTrue;
	unsigned	to_nodes, from_nodes;

	assert (er != NULL && "Bad rule?");

	rule_expr = er->materialize();
	if (rule_expr.isNull()) {
		os << "No materialize\n";
		return false;
	}

	to_nodes = ExprUtil::getNumNodes(er->getCleanToExpr());
	from_nodes = ExprUtil::getNumNodes(er->getCleanFromExpr());

	ok = s->mustBeTrue(Query(rule_expr), mustBeTrue);
	if (ok == false) {
		os << "query failure\n";
		return false;
	}

	if (er->getToExpr() == er->getFromExpr()) {
		os << "identity rule\n";
		return false;
	}

	if (to_nodes == from_nodes) {
		os << "not-better rule\n";
		return false;
	}

	if (to_nodes > from_nodes) {
		os << "expanding rule\n";
		return false;
	}

	if (mustBeTrue == false) {
		os << "invalid rule\n";
		return false;
	}

	os << "valid rule\n";
	return true;
}

static bool checkRule(ExprBuilder *eb, Solver* s)
{
	ExprRule	*er = ExprRule::loadRule(InputFile.c_str());
	bool		ok;

	ok = checkRule(er, s, std::cout);
	delete er;

	return ok;
}

static void applyRule(ExprBuilder *eb, Solver* s)
{
	ExprRule	*er;
	SMTParser	*p;
	ref<Expr>	applied_expr;
	bool		ok, mustBeTrue;
	ref<Expr>	e, cond;

	er = ExprRule::loadRule(ApplyRule.c_str());
	assert (er != NULL && "Bad rule?");

	p = SMTParser::Parse(InputFile.c_str(), eb);
	assert (p != NULL && "Could not load SMT");

	applied_expr = er->apply(p->satQuery);
	e = p->satQuery;
	if (applied_expr.isNull()) {
		ref<Expr>	lhs, rhs;
		getEquivalence(p->satQuery, lhs, rhs);

		e = lhs;
		applied_expr = er->apply(e);
		if (applied_expr.isNull()) {
			e = rhs;
			applied_expr = er->apply(e);
		}

		if (applied_expr.isNull()) {
			std::cout << "Could not apply rule.\n";
			return;
		}
	}

	cond = MK_EQ(applied_expr, e);
	std::cerr << "CHECKING APPLICATION: " << cond << '\n';

	ok = s->mustBeTrue(Query(cond), mustBeTrue);
	assert (ok && "Unhandled solver failure");

	if (mustBeTrue) {
		std::cout << "valid apply\n";
	} else {
		std::cout << "invalid apply\n";
	}

	delete er;
}

static void printRule(ExprBuilder *eb, Solver* s)
{
	ref<Expr>	lhs, rhs;
	bool		ok, mustBeTrue;
	SMTParser	*p;

	p = SMTParser::Parse(InputFile.c_str(), eb);
	if (p == NULL) {
		std::cerr << "[kopt] Could not parse '" << InputFile << "'\n";
		return;
	}

	std::cerr << p->satQuery << "!!!\n";
	getEquivalence(p->satQuery, lhs, rhs);

	ok = s->mustBeTrue(Query(MK_EQ(lhs, rhs)), mustBeTrue);
	if (!ok) {
		std::cerr << "[kopt] Solver failed\n";
		delete p;
		return;
	}

	assert (mustBeTrue && "We've proven this valid, but now it isn't?");

	ExprRule::printRule(std::cout, lhs, rhs);
	delete p;
}

static void addRule(ExprBuilder* eb, Solver* s)
{
	ExprRule	*er;
	RuleBuilder	*rb;

	/* bogus rule? */
	if (checkRule(eb, s) == false)
		return;

	/* dup? */
	if (checkDup(eb, s) == true)
		return;

	/* otherwise, append! */
	er = ExprRule::loadRule(InputFile.c_str());
	assert (er);

	rb = RuleBuilder::create(ExprBuilder::create(BuilderKind));
	std::ofstream	of(
		rb->getDBPath().c_str(),
		std::ios_base::out |
		std::ios_base::app |
		std::ios_base::binary);

	er->printPrettyRule(std::cerr);
	er->printBinaryRule(of);
	of.close();

	delete rb;

	std::cout << "Add OK\n";
}

static void dumpRuleDir(Solver* s)
{
	DIR		*d;
	struct dirent	*de;

	d = opendir(InputFile.c_str());
	assert (d != NULL);
	while ((de = readdir(d)) != NULL) {
		ExprRule	*er;
		char		path[256];

		snprintf(path, 256, "%s/%s", InputFile.c_str(), de->d_name);
		er = ExprRule::loadRule(path);
		if (er == NULL)
			continue;

		if (CheckRule && !checkRule(er, s, std::cerr)) {
			delete er;
			continue;
		}

		er->printBinaryRule(std::cout);
		delete er;
	}

	closedir(d);
}

static void dumpRule(Solver* s)
{
	struct stat	st;

	if (stat(InputFile.c_str(), &st) != 0) {
		std::cerr << "[kopt] " << InputFile << " not found.\n";
		return;
	}

	if (S_ISREG(st.st_mode)) {
		ExprRule	*er;
		er = ExprRule::loadRule(InputFile.c_str());
		if (CheckRule && !checkRule(er, s, std::cerr))
			return;

		er->printBinaryRule(std::cout);
		return;
	}

	if (!S_ISDIR(st.st_mode)) {
		std::cerr << "[kopt] Expected file or directory\n";
		return;
	}

	std::cerr << "[kopt] Dumping rule dir\n";
	dumpRuleDir(s);
}

void dumpPattern(void)
{
	RuleBuilder	*rb;

	rb = RuleBuilder::create(ExprBuilder::create(BuilderKind));
	foreach (it, rb->begin(), rb->end()) {
		std::cout << "FROM: ";
		(*it)->getFromPattern().dump(std::cout);
		std::cout << "TO: ";
		(*it)->getToPattern().dump(std::cout);
		std::cout << "=======================\n";
	}
	delete rb;
}

void dumpDB(void)
{
	RuleBuilder	*rb;

	rb = RuleBuilder::create(ExprBuilder::create(BuilderKind));
	foreach (it, rb->begin(), rb->end())
		(*it)->print(std::cout);
	delete rb;
}

static void dedupDB(void)
{
	std::ofstream		of(InputFile.c_str());
	RuleBuilder		*rb;
	std::set<ExprRule>	ers;
	unsigned		i;

	rb = RuleBuilder::create(ExprBuilder::create(BuilderKind));
	i = 0;
	foreach (it, rb->begin(), rb->end()) {
		const ExprRule	*er(*it);
		if (ers.count(*er)) {
			i++;
			continue;
		}
		er->printBinaryRule(of);
		ers.insert(*er);
	}

	delete rb;

	std::cout << "Dups found: " << i << '\n';
}

static void eraseShadowRules(Solver* s)
{
	ExprBuilder			*init_eb;
	RuleBuilder			*rb;
	std::set<const ExprRule*>	del_rules;

	rb = RuleBuilder::create(ExprBuilder::create(BuilderKind));
	init_eb = Expr::getBuilder();

	foreach (it, rb->begin(), rb->end()) {
		const ExprRule	*er(*it), *last_er;
		BuiltRule	br(init_eb, rb, er);

		last_er = RuleBuilder::getLastRule();
		if (last_er == er)
			continue;

		/* some other rule is doing the work! great */
		if (br.builtAsExpected()) {
			/* I'd much prefer constrained rules
			 * since they are known to cover more rules;
			 * may not want this if rule is hopelessly masked.
			 * How can we test if it's masked? */
			if (er->hasConstraints()&&!last_er->hasConstraints()) {
				if (!del_rules.count(last_er))
					continue;
				del_rules.insert(last_er);
				rb->eraseDBRule(last_er);
				std::cerr << "Overconstrained\n";
			}

			continue;
		}

		if (br.isIneffective()) {
			std::cerr << "Translation didn't take. Ulp\n";
			continue;
		}

		if (br.isBetter()) {
			/* rule is obsolete; did better than expected */
			if (!del_rules.count(er)) {
				rb->eraseDBRule(er);
				del_rules.insert(er);
				std::cerr << "Obsolete\n";
			}
			continue;
		}

		if (!br.isWorse()) {
			/* duplicate rule */
			if (er->hasConstraints()&&!last_er->hasConstraints())
				continue;

			if (!del_rules.count(er)) {
				rb->eraseDBRule(er);
				del_rules.insert(er);
				std::cerr << "Shadowed\n";
			}
		}
	}

	delete rb;
}

/* verify that the rule data base is properly translating rules */
extern int xxx_rb;
static void checkDB(Solver* s)
{
	ExprBuilder	*init_eb;
	RuleBuilder	*rb;
	unsigned	i;
	unsigned	unexpected_from_c, better_from_c, bad_verify_c;

	rb = RuleBuilder::create(ExprBuilder::create(BuilderKind));
	init_eb = Expr::getBuilder();

	i = 0;
	unexpected_from_c = 0;
	better_from_c = 0;
	bad_verify_c = 0;

	foreach (it, rb->begin(), rb->end()) {
		const ExprRule	*er(*it);
		bool		bad;
		BuiltRule	br(init_eb, rb, er);

		i++;
		xxx_rb = i;

		if (br.builtAsExpected()) {
			if (VerifyDB) {
				if (checkRule(er, s, std::cerr) == false)
					bad_verify_c++;
			}
			continue;
		}

		std::cerr << "=======================\n";
		std::cerr << "!DIDN'T XLATE AS EXPECTED #" << i << "!\n";
		br.dump(std::cerr);

		if (br.isBetter()) better_from_c++;

		std::cerr << "EXPECTED RULE:\n";
		er->print(std::cerr);
		std::cerr << '\n';

		er = RuleBuilder::getLastRule();
		if (er == NULL)
			continue;

		if (er != NULL) {
			std::cerr << "LAST RULE APPLIED:\n";
			er->print(std::cerr);
			std::cerr << '\n';
		}

		std::cerr << "=======================\n";

		unexpected_from_c++;

		if (VerifyDB) {
			bad = (checkRule(er, s, std::cerr) == false);
			if (!getExprCex(s, br.getToActual(), br.getFrom(), std::cerr))
				bad = true;

			/* terminate if bad rule */
			if (bad) {
				std::cerr << "!! BAD VERIFY !!\n";
				bad_verify_c++;
			}
		}

		if (br.isReduced())
			continue;

		std::cerr << "DID NOT TRANSLATE #" << i << ":\n";
		br.dump(std::cerr);
		std::cerr << "LAST RULE:\n";
		er->print(std::cerr);
		std::cerr << '\n';
		assert (0 == 1);
	}

	delete rb;
	std::cout << "PASSED CHECKDB. NUM-RULES=" << i
		<< ". UNEXPECTED-FROM-EXPRS=" << unexpected_from_c
		<< ". BETTER-FROM-EXPRS=" << better_from_c;

	if (VerifyDB)
		std::cout << ". BAD-VERIFY=" << bad_verify_c;

	std::cout << ".\n";
}

static void extractRule(unsigned rule_num)
{
	std::ofstream	of(InputFile.c_str());
	RuleBuilder	*rb;
	unsigned	i;

	rb = RuleBuilder::create(ExprBuilder::create(BuilderKind));
	if (rb->size() < rule_num) {
		std::cerr << "Could not extract rule #" << rule_num
			<< " from total of " << rb->size() << "rules.\n";
		delete rb;
		return;
	}

	i = 0;
	foreach (it, rb->begin(), rb->end()) {
		const ExprRule	*er(*it);

		if (i == rule_num) {
			er->printBinaryRule(of);
			break;
		}
		i++;
	}

	delete rb;
}

static void extractConstrs(const std::string& fname)
{
	std::ofstream	of(fname.c_str());
	RuleBuilder	*rb;

	rb = RuleBuilder::create(ExprBuilder::create(BuilderKind));
	foreach (it, rb->begin(), rb->end()) {
		const ExprRule	*er(*it);
		if (!er->hasConstraints())
			continue;
		er->printBinaryRule(of);
	}

	delete rb;
}

static void extractFree(const std::string& fname)
{
	std::ofstream	of(fname.c_str());
	RuleBuilder	*rb;

	rb = RuleBuilder::create(ExprBuilder::create(BuilderKind));
	foreach (it, rb->begin(), rb->end()) {
		const ExprRule	*er(*it);
		if (!er->hasFree())
			continue;
		er->printBinaryRule(of);
	}

	delete rb;
}

/* counts how many challenge file rules match the
 * rule-file database */
static void compareDBs(const std::string& fname)
{
	RuleBuilder	*rb_db, *rb_in;
	unsigned	matches = 0;

	rb_db = RuleBuilder::create(ExprBuilder::create(BuilderKind));
	rb_in = RuleBuilder::create(
		ExprBuilder::create(BuilderKind),
		fname.c_str());

	foreach (it, rb_in->begin(), rb_in->end()) {
		if (rb_db->hasExprRule(*it))
			matches++;
	}

	std::cout << "Matched: " << matches  << " / " << rb_in->size() << '\n';

	delete rb_db;
	delete rb_in;
}

static void splitDB(std::string& prefix, int num_chunks)
{
	RuleBuilder	*rb;
	std::ofstream	**ofs;
	unsigned	rules_per_chunk;
	unsigned	rule_c;

	assert (num_chunks > 1);
	assert (!prefix.empty());

	ofs = new std::ofstream*[num_chunks];
	for (int i = 0; i < num_chunks; i++)
		ofs[i] = new std::ofstream(
			((prefix + ".") + llvm::utostr(i)).c_str());

	rb = RuleBuilder::create(ExprBuilder::create(BuilderKind));
	rules_per_chunk = (rb->size()+num_chunks-1) / num_chunks;
	rule_c = 0;
	foreach (it, rb->begin(), rb->end()) {
		const ExprRule	*er(*it);
		unsigned	chunk_num;

		chunk_num = rule_c / rules_per_chunk;
		er->printBinaryRule(*ofs[chunk_num]);
		rule_c++;
	}

	delete rb;
	for (int i = 0; i < num_chunks; i++)
		delete ofs[i];
	delete [] ofs;
}

int main(int argc, char **argv)
{
	Solver		*s;
	ExprBuilder	*eb;

	llvm::sys::PrintStackTraceOnErrorSignal();
	llvm::cl::ParseCommandLineOptions(argc, argv);

	eb = ExprBuilder::create(BuilderKind);
	if (eb == NULL) {
		std::cerr << "[kopt] Could not create builder\n";
		return -1;
	}

	s = Solver::createChain();
	if (s == NULL) {
		std::cerr << "[kopt] Could not initialize solver\n";
		return -3;
	}

	if (CompareDBs) {
		compareDBs(InputFile);
	} else if (ExtractFrees) {
		extractFree(InputFile);
	} else if (DumpPattern) {
		dumpPattern();
	} else if (EraseShadows) {
		eraseShadowRules(s);
	} else if (SplitDB) {
		splitDB(InputFile, SplitDB);
	} else if (ExtractConstrs) {
		extractConstrs(InputFile);
	} else if (DedupDB) {
		dedupDB();
	} else if (ExtractRule != -1) {
		extractRule(ExtractRule);
	} else if (DumpDB) {
		dumpDB();
	} else if (AddRule) {
		addRule(eb, s);
	} else if (VerifyDB || CheckDB) {
		checkDB(s);
	} else if (BenchRB) {
		Benchmarker	bm(s, BuilderKind);
		bm.benchRuleBuilder(eb);
	} else if (BenchmarkRules) {
		Benchmarker	bm(s, BuilderKind);
		bm.benchRules();
	} else if (DBHisto) {
		DBScan	dbs(s);
		dbs.histo();
	} else if (DBPunchout) {
		DBScan		dbs(s);
		std::ofstream	of(InputFile.c_str());
		dbs.punchout(of);
	} else if (CheckDup) {
		checkDup(eb, s);
	} else if (BRuleRebuild) {
		rebuildBRules(s, InputFile);
	} else if (BRuleXtive) {
		xtiveBRule(eb, s);
	} else if (DumpBinRule) {
		dumpRule(s);
	} else if (CheckRule) {
		checkRule(eb, s);
	} else if (ApplyRule.size()) {
		applyRule(eb, s);
	} else {
		printRule(eb, s);
	}

	delete s;
	delete eb;
	llvm::llvm_shutdown();
	return 0;
}

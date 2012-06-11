#include <iostream>
#include <sstream>

#include <llvm/Support/CommandLine.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/system_error.h>

#include "../../lib/Expr/SMTParser.h"
#include "../../lib/Expr/ExprRule.h"
#include "../../lib/Expr/RuleBuilder.h"
#include "../../lib/Expr/OptBuilder.h"
#include "../../lib/Expr/ExtraOptBuilder.h"
#include "../../lib/Core/TimingSolver.h"

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

#include "Benchmarker.h"
#include "DBScan.h"

using namespace llvm;
using namespace klee;
using namespace klee::expr;

ExprBuilder::BuilderKind	BuilderKind;

namespace llvm
{
	cl::opt<std::string>
	InputFile(
		cl::desc("<equivexpr proof .smt>"),
		cl::Positional,
		cl::init("-"));

	cl::opt<bool>
	BenchRB(
		"benchmark-rb",
		cl::desc("Benchmark rule builder with random queries"),
		cl::init(false));

	cl::opt<bool>
	BenchmarkRules(
		"benchmark-rules",
		cl::desc("Benchmark rules with random queries"),
		cl::init(false));

	cl::opt<bool>
	DBPunchout(
		"db-punchout",
		cl::desc("Punch out constants in DB rules"),
		cl::init(false));

	cl::opt<bool>
	DBHisto(
		"db-histo",
		cl::desc("Print histogram of DB rule classes"),
		cl::init(false));

	cl::opt<bool>
	CheckRule(
		"check-rule",
		cl::desc("Check a rule file"),
		cl::init(false));

	cl::opt<bool>
	CheckDB(
		"check-db",
		cl::desc("Verify that rule database is working"),
		cl::init(false));

	cl::opt<bool>
	CheckDup(
		"check-dup",
		cl::desc("Check if duplicate rule"),
		cl::init(false));

	cl::opt<std::string>
	ApplyRule(
		"apply-rule",
		cl::desc("Apply given rule file to input smt"),
		cl::init(""));

	cl::opt<bool>
	DumpBinRule(
		"dump-bin",
		cl::desc("Dump rule in binary format."),
		cl::init(false));

	cl::opt<bool>
	DumpDB(
		"dump-db",
		cl::desc("Dump rule db in pretty format."),
		cl::init(false));

	cl::opt<bool>
	BRuleXtive(
		"brule-xtive",
		cl::desc("Search for transitive rules in brule database"),
		cl::init(false));

	cl::opt<bool>
	BRuleRebuild(
		"brule-rebuild",
		cl::desc("Rebuild brule file."),
		cl::init(false));

	cl::opt<int>
	ExtractRule(
		"extract-rule",
		cl::desc("Extract rule from brule file."),
		cl::init(-1));


	cl::opt<bool>
	AddRule(
		"add-rule",
		cl::desc("Add rule to brule file."),
		cl::init(false));

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
}


void xtiveBRule(ExprBuilder *eb, Solver* s);
bool checkRule(const ExprRule* er, Solver* s, std::ostream&);

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
	rhs = ConstantExpr::create(1, 1);

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

	rb = new RuleBuilder(ExprBuilder::create(BuilderKind));
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

bool checkRule(const ExprRule* er, Solver* s, std::ostream& os)
{
	ref<Expr>	rule_expr;
	bool		ok, mustBeTrue;
	unsigned	to_nodes, from_nodes;

	assert (er != NULL && "Bad rule?");

	rule_expr = er->materialize();

	to_nodes = ExprUtil::getNumNodes(er->getToExpr());
	from_nodes = ExprUtil::getNumNodes(er->getFromExpr());

	ok = s->mustBeTrue(Query(rule_expr), mustBeTrue);
	if (ok == false) {
		os << "query failure\n";
		return false;
	}

	if (er->getToExpr() == er->getFromExpr()) {
		os << "identity rule\n";
		return false;
	}

	if (to_nodes >= from_nodes) {
		os << "non-shrinking rule\n";
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

	cond = EqExpr::create(applied_expr, e);
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

	ok = s->mustBeTrue(Query(EqExpr::create(lhs, rhs)), mustBeTrue);
	if (!ok) {
		std::cerr << "[kopt] Solver failed\n";
		delete p;
		return;
	}

	assert (mustBeTrue && "We've proven this valid, but now it isn't?");

	ExprRule::printRule(std::cout, lhs, rhs);
	delete p;
}

static void rebuildBRule(
	ExprBuilder* eb,
	Solver *s,
	const ExprRule* er,
	std::ostream& of)
{
	std::stringstream	ss;
	ExprRule		*er_rebuild;

	if (checkRule(er, s, std::cout) == false) {
		std::cerr << "BAD RULE:\n";
		er->print(std::cerr);
		return;
	}

	er->printBinaryRule(ss);
	er_rebuild = ExprRule::loadBinaryRule(ss);

	/* ensure we haven't corrupted the from-expr--
	 * otherwise, it might not match during runtime! */
	if (	ExprUtil::getNumNodes(er_rebuild->getFromExpr()) !=
		ExprUtil::getNumNodes(er->getFromExpr()))
	{
		std::cerr << "BAD REBUILD:\n";
		std::cerr << "ORIGINAL:\n";
		er->print(std::cerr);
		std::cerr << "NEW:\n";
		er_rebuild->print(std::cerr);

		std::cerr	<< "ORIG-EXPR: "
				<< er->getFromExpr() << '\n'
				<< "NEW-EXPR: "
				<< er_rebuild->getFromExpr() << '\n';
		delete er_rebuild;
		return;
	}

	er_rebuild->printBinaryRule(of);
	delete er_rebuild;
}

static void rebuildBRules(ExprBuilder* eb, Solver* s)
{
	std::ofstream		of(InputFile.c_str());
	RuleBuilder		*rb;
	unsigned		i;

	assert (of.good() && !of.fail());

	rb = new RuleBuilder(ExprBuilder::create(BuilderKind));

	i = 0;
	foreach (it, rb->begin(), rb->end()) {
		std::cerr << "[" << ++i << "]: ";
		rebuildBRule(eb, s, *it, of);
	}

	delete rb;
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

	rb = new RuleBuilder(ExprBuilder::create(BuilderKind));
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

void dumpDB(void)
{
	RuleBuilder	*rb;

	rb = new RuleBuilder(ExprBuilder::create(BuilderKind));
	foreach (it, rb->begin(), rb->end())
		(*it)->printPrettyRule(std::cout);
	delete rb;
}

/* verify that the rule data base is properly translating rules */
static void checkDB(Solver* s)
{
	ExprBuilder	*init_eb;
	RuleBuilder	*rb;
	unsigned	i;
	unsigned	unexpected_from_c, better_from_c;

	rb = new RuleBuilder(ExprBuilder::create(BuilderKind));
	init_eb = Expr::getBuilder();
	i = 0;
	unexpected_from_c = 0;
	better_from_c = 0;

	foreach (it, rb->begin(), rb->end()) {
		const ExprRule	*er(*it);
		ref<Expr>	from_eb, from_rb, to_e;

		i++;

		to_e = er->getToExpr();
		from_eb = er->getFromExpr();

		Expr::setBuilder(rb);
		from_rb = er->getFromExpr();
		Expr::setBuilder(init_eb);

		if (from_rb == to_e)
			continue;


		if (from_rb != from_eb) {
			unsigned	to_node_c, from_node_c;

			std::cerr << "=======================\n";
			std::cerr << "!!!DID NOT TRANSLATE AS EXPECTED!!!!\n";
			std::cerr << "FROM-EXPR-EB=" << from_eb << '\n';
			std::cerr << "FROM-EXPR-RB=" << from_rb << '\n';

			to_node_c = ExprUtil::getNumNodes(to_e);
			from_node_c = ExprUtil::getNumNodes(from_rb);

			if (to_node_c > from_node_c)
				better_from_c++;

			std::cerr << "EXPECTED RULE:\n";
			er->print(std::cerr);
			std::cerr << '\n';

			er = RuleBuilder::getLastRule();
			if (er != NULL) {
				std::cerr << "LAST RULE APPLIED:\n";
				er->print(std::cerr);
				std::cerr << '\n';
			}

			std::cerr << "=======================\n";

			unexpected_from_c++;
			continue;
		}

		std::cerr << "DID NOT TRANSLATE #" << i << ":\n";
		std::cerr << "FROM-EXPR-EB: " << from_eb << '\n';
		std::cerr << "FROM-EXPR-RB: " << from_rb << '\n';
		std::cerr << "TO-EXPR: " << to_e << '\n';
		std::cerr << "RULE:\n";
		er->print(std::cerr);
		std::cerr << '\n';
		assert (0 == 1);
	}

	delete rb;
	std::cout << "PASSED CHECKDB. NUM-RULES=" << i
		<< ". UNEXPECTED-FROM-EXPRS=" << unexpected_from_c
		<< ". BETTER-FROM-EXPRS=" << better_from_c
		<< ".\n";
}

static void extractRule(unsigned rule_num)
{
	std::ofstream	of(InputFile.c_str());
	RuleBuilder	*rb;
	unsigned	i;

	rb = new RuleBuilder(ExprBuilder::create(BuilderKind));
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

	if (ExtractRule != -1) {
		extractRule(ExtractRule);
	} else if (DumpDB) {
		dumpDB();
	} else if (AddRule) {
		addRule(eb, s);
	} else if (CheckDB) {
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
		rebuildBRules(eb, s);
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

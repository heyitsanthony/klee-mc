#include <iostream>
#include <sstream>

#include "DBScan.h"
#include "../../lib/Expr/SMTParser.h"
#include "../../lib/Expr/ExprRule.h"
#include "../../lib/Expr/RuleBuilder.h"
#include "../../lib/Expr/OptBuilder.h"
#include "../../lib/Expr/ExtraOptBuilder.h"
#include "../../lib/Core/TimingSolver.h"

#include "static/Sugar.h"
#include "klee/ExprBuilder.h"
#include "klee/Solver.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/Assignment.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/system_error.h"

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
	CheckRule(
		"check-rule",
		cl::desc("Check a rule file"),
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
	ApplyTransitivity(
		"apply-transitive",
		cl::desc("Use a rule builder to try to minimize rule"),
		cl::init(false));


	cl::opt<std::string>
	TransitiveRuleFile(
		"implied-rule-file",
		cl::desc("Use a rule builder to try to minimize rule"));

	cl::opt<bool>
	DumpBinRule(
		"dump-bin",
		cl::desc("Dump rule in binary format."),
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

	cl::opt<bool>
	AddRule(
		"add-rule",
		cl::desc("Add rule to brule file."),
		cl::init(false));


	static cl::opt<ExprBuilder::BuilderKind,true>
	BuilderKindProxy("builder",
		cl::desc("Expression builder:"),
		cl::location(BuilderKind),
		cl::init(ExprBuilder::SimplifyingBuilder),
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
bool checkRule(const ExprRule* er, Solver* s);
void benchmarkRules(ExprBuilder *eb, Solver* s);

/*
(= (ite (bvult
		(bvadd
			bv2936[64]
			( zero_extend[56]
				( select ?e2179  bv37[32] )))
		bv3840[64])
	bv1[1]
	bv0[1])
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

	if (!er)
		return false;

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

bool checkRule(const ExprRule* er, Solver* s)
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
		std::cout << "query failure\n";
		return false;
	}

	if (er->getToExpr() == er->getFromExpr()) {
		std::cout << "identity rule\n";
		return false;
	}

	if (to_nodes >= from_nodes) {
		std::cout << "non-shrinking rule\n";
		return false;
	}

	if (mustBeTrue == false) {
		std::cout << "invalid rule\n";
		return false;
	}

	std::cout << "valid rule\n";
	return true;
}

static bool checkRule(ExprBuilder *eb, Solver* s)
{
	ExprRule	*er = ExprRule::loadRule(InputFile.c_str());
	bool		ok;

	ok = checkRule(er, s);
	delete er;

	return ok;
}

static void applyTransitivity(ExprBuilder* eb, Solver* s)
{
	ExprBuilder	*rule_eb, *old_eb;
	ExprRule	*er;
	ref<Expr>	rule_to_rb, rule_from_rb;
	ref<Expr>	rule_to_old, rule_from_old;
	ref<Expr>	init_expr, bridge_expr, impl_expr, new_rule_expr;
	bool		ok, mustBeTrue;

	er = ExprRule::loadRule(InputFile.c_str());
	assert (er != NULL && "Bad rule?");

	rule_from_old = er->getFromExpr();
	rule_to_old = er->getToExpr();

	rule_eb = new RuleBuilder(ExprBuilder::create(BuilderKind));
	old_eb = Expr::setBuilder(rule_eb);
	rule_from_rb = er->getFromExpr();
	rule_to_rb = er->getToExpr();
	Expr::setBuilder(old_eb);

	if (	rule_to_old.isNull() || rule_from_old.isNull() ||
		rule_to_rb.isNull() || rule_from_rb.isNull())
	{
		std::cout << "could not build rule exprs\n";
		goto done;
	}

	if (rule_from_rb == rule_from_old) {
		std::cout << "rule builder broken. got same materialization\n";

		std::cerr	<< "OLD: " << rule_from_old
				<< "\n-> " << rule_to_old << '\n';

		std::cerr	<< "RULE:" << rule_from_rb
				<< "\n-> " << rule_to_rb << '\n';

		goto done;
	}

	/* bridge expr is shared expr */
	if (rule_from_rb != rule_from_old && rule_from_rb != rule_to_old)
		bridge_expr = rule_to_rb;
	else
		bridge_expr = rule_from_rb;

	/* expect that for nonopt -> opt,
	 * rule builder will give opt -> opt */
	if (bridge_expr == rule_from_rb && bridge_expr == rule_to_rb) {
		std::cout << "true\n(bridge_expr=" << bridge_expr << ")\n";
		std::cout << "(from_old=" << rule_from_old << ")\n";
		goto done;
	}

	init_expr = (bridge_expr != rule_from_old)
		? rule_from_old
		: rule_to_old;

	impl_expr = (bridge_expr == rule_from_rb)
		? rule_to_rb
		: rule_from_rb;

	new_rule_expr = EqExpr::create(init_expr, impl_expr);
	ok = s->mustBeTrue(Query(new_rule_expr), mustBeTrue);

	if (!ok) {
		std::cout << "query failure\n";
	} else if (mustBeTrue) {
		if (!TransitiveRuleFile.empty()) {
			std::ofstream	ofs(TransitiveRuleFile.c_str());
			ExprRule::printRule(ofs, init_expr, impl_expr);
		}
		ExprRule::printRule(std::cout, init_expr, impl_expr);
	} else {
		std::cout << "invalid rule\n";
	}

done:
	delete rule_eb;
	delete er;
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

static void rebuildBRule(ExprBuilder* eb, Solver* s)
{
	std::ofstream		of(InputFile.c_str());
	RuleBuilder		*rb;
	unsigned		i;

	assert (of.good() && !of.fail());

	rb = new RuleBuilder(ExprBuilder::create(BuilderKind));
	i = 0;

	foreach (it, rb->begin(), rb->end()) {
		const ExprRule	*er = *it;
		ExprRule	*er_rebuild;

		std::cerr << "[" << ++i << "]: ";
		if (checkRule(er, s) == false) {
			std::cerr << "BAD RULE:\n";
			er->printPrettyRule(std::cerr);
			continue;
		}

		std::stringstream	ss;

		er->printPrettyRule(ss);
		er_rebuild = ExprRule::loadPrettyRule(ss);

		/* ensure we haven't corrupted the from-expr--
		 * otherwise, it might not match during runtime! */
		if (	ExprUtil::getNumNodes(er_rebuild->getFromExpr()) !=
			ExprUtil::getNumNodes(er->getFromExpr()))
		{
			std::cerr << "BAD REBUILD:\n";
			std::cerr << "ORIGINAL:\n";
			er->printPrettyRule(std::cerr);
			std::cerr << "NEW:\n";
			er_rebuild->printPrettyRule(std::cerr);

			std::cerr	<< "ORIG-EXPR: "
					<< er->getFromExpr() << '\n'
					<< "NEW-EXPR: "
					<< er_rebuild->getFromExpr() << '\n';
			delete er_rebuild;
			continue;
		}

		er_rebuild->printBinaryRule(of);
		delete er_rebuild;
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

	if (AddRule) {
		addRule(eb, s);
	} else if (BenchmarkRules) {
		benchmarkRules(eb, s);
	} else if (DBPunchout) {
		DBScan	dbs(s);
		dbs.punchout();
	} else if (CheckDup) {
		checkDup(eb, s);
	} else if (BRuleRebuild) {
		rebuildBRule(eb, s);
	} else if (BRuleXtive) {
		xtiveBRule(eb, s);
	} else if (CheckRule) {
		checkRule(eb, s);
	} else if (ApplyRule.size()) {
		applyRule(eb, s);
	} else if (ApplyTransitivity) {
		applyTransitivity(eb, s);
	} else if (DumpBinRule) {
		ExprRule	*er;
		er = ExprRule::loadRule(InputFile.c_str());
		er->printBinaryRule(std::cout);
	} else {
		printRule(eb, s);
	}

	delete s;
	delete eb;
	llvm::llvm_shutdown();
	return 0;
}

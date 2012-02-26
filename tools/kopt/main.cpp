#include <iostream>

#include "../../lib/Expr/SMTParser.h"
#include "../../lib/Expr/ExprRule.h"
#include "../../lib/Expr/RuleBuilder.h"
#include "../../lib/Core/TimingSolver.h"

#include "static/Sugar.h"
#include "klee/ExprBuilder.h"
#include "klee/Solver.h"
#include "klee/util/ExprUtil.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/system_error.h"

using namespace llvm;
using namespace klee;
using namespace klee::expr;

namespace llvm
{
	cl::opt<std::string>
	InputFile(
		cl::desc("<equivexpr proof .smt>"),
		cl::Positional,
		cl::init("-"));

	cl::opt<bool>
	CheckRule(
		"check-rule",
		cl::desc("Check a rule file"),
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

	enum BuilderKinds {
		DefaultBuilder,
		ConstantFoldingBuilder,
		SimplifyingBuilder
	};

	static cl::opt<BuilderKinds>
	BuilderKind("builder",
		cl::desc("Expression builder:"),
		cl::init(DefaultBuilder),
		cl::values(
			clEnumValN(DefaultBuilder, "default",
			"Default expression construction."),
			clEnumValN(ConstantFoldingBuilder, "constant-folding",
			"Fold constant expressions."),
			clEnumValN(SimplifyingBuilder, "simplify",
			"Fold constants and simplify expressions."),
			clEnumValEnd));
}

static ExprBuilder* createExprBuilder(void)
{
	ExprBuilder *Builder = createDefaultExprBuilder();
	switch (BuilderKind) {
	case DefaultBuilder:
		break;
	case ConstantFoldingBuilder:
		Builder = createConstantFoldingExprBuilder(Builder);
		break;
	case SimplifyingBuilder:
		Builder = createConstantFoldingExprBuilder(Builder);
		Builder = createSimplifyingExprBuilder(Builder);
		break;
	}

	return Builder;
}

static void getEquivalence(ref<Expr> e, ref<Expr>& lhs, ref<Expr>& rhs)
{
	const ConstantExpr	*ce;
	const EqExpr		*ee;
	const SelectExpr	*se;

	ee = dyn_cast<EqExpr>(e);
	assert (ee && "Expected TLS to be EqExpr!");

	ce = dyn_cast<ConstantExpr>(ee->getKid(1));
	assert (ce && ce->getZExtValue() == 0);

	se = dyn_cast<SelectExpr>(ee->getKid(0));
	assert (se && "Expected (Eq (Sel x) 0)");

	ce = dyn_cast<ConstantExpr>(se->trueExpr);
	assert (ce && ce->getZExtValue() == 1);

	ce = dyn_cast<ConstantExpr>(se->falseExpr);
	assert (ce && ce->getZExtValue() == 0);

	ee = dyn_cast<EqExpr>(se->cond);
	assert (ee && "Expected (Eq (Sel (Eq x y) 1 0) 0)");

	lhs = ee->getKid(0);
	rhs = ee->getKid(1);
}

static void checkRule(ExprBuilder *eb, Solver* s)
{
	ExprRule	*er = ExprRule::loadPrettyRule(InputFile.c_str());
	ref<Expr>	rule_expr;
	bool		ok, mustBeTrue;
	unsigned	to_nodes, from_nodes;

	assert (er != NULL && "Bad rule?");

	rule_expr = er->materialize();
	std::cerr << rule_expr << '\n';

	to_nodes = ExprUtil::getNumNodes(er->getToExpr());
	from_nodes = ExprUtil::getNumNodes(er->getFromExpr());

	ok = s->mustBeTrue(Query(rule_expr), mustBeTrue);
	assert (ok && "Unhandled solver failure");

	if (mustBeTrue) {
		std::cout << "valid rule\n";
	} else if (to_nodes >= from_nodes) {
		std::cout << "non-shrinking rule\n";
	} else {
		std::cout << "invalid rule\n";
	}

	delete er;
}

static void applyTransitivity(ExprBuilder* eb, Solver* s)
{
	ExprBuilder	*rule_eb, *old_eb;
	ExprRule	*er;
	ref<Expr>	rule_expr_old, rule_expr_rb;
	ref<Expr>	init_expr, bridge_expr, impl_expr, new_rule_expr;
	bool		ok, mustBeTrue;

	er = ExprRule::loadPrettyRule(InputFile.c_str());
	assert (er != NULL && "Bad rule?");

	rule_expr_old = er->materialize();

	rule_eb = new RuleBuilder(createExprBuilder());
	old_eb = Expr::setBuilder(rule_eb);
	rule_expr_rb = er->materialize();
	Expr::setBuilder(old_eb);

	if (isa<ConstantExpr>(rule_expr_rb)) {
		std::cout << "true\n";
		goto done;
	}

	if (rule_expr_rb == rule_expr_old) {
		std::cout << "rule builder broken. got same materialization\n";
		goto done;
	}

	/* bridge expr is shared expr */
	if (	rule_expr_rb->getKid(0) != rule_expr_old->getKid(0) &&
		rule_expr_rb->getKid(0) != rule_expr_old->getKid(1))

		bridge_expr = rule_expr_rb->getKid(1);
	else
		bridge_expr = rule_expr_rb->getKid(0);

	/* expect that for (eq nonopt opt),
	 * rule builder will give (eq opt opt) */
	if (	bridge_expr == rule_expr_rb->getKid(0) &&
		bridge_expr == rule_expr_rb->getKid(1))
	{
		std::cout << "true\n";
		goto done;
	}

	init_expr = (bridge_expr != rule_expr_old->getKid(0))
		? rule_expr_old->getKid(0)
		: rule_expr_old->getKid(1);

	impl_expr = (bridge_expr == rule_expr_rb->getKid(0))
		? rule_expr_rb->getKid(1)
		: rule_expr_rb->getKid(0);

	new_rule_expr = EqExpr::create(init_expr, impl_expr);
	ok = s->mustBeTrue(Query(new_rule_expr), mustBeTrue);
	assert (ok && "Unhandled solver failure");

	if (mustBeTrue) {
		std::cout << "valid rule\n";
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

	er = ExprRule::loadPrettyRule(ApplyRule.c_str());
	assert (er != NULL && "Bad rule?");

	p = SMTParser::Parse(InputFile.c_str(), eb);
	assert (p != NULL && "Could not load SMT");

	applied_expr = er->apply(p->satQuery);
	e = p->satQuery;
	if (applied_expr.isNull()) {
		ref<Expr>	lhs, rhs;
		getEquivalence(p->satQuery, lhs, rhs);

		applied_expr = er->apply(lhs);
		e = lhs;
		if (applied_expr.isNull()) {
			applied_expr = er->apply(rhs);
			e = rhs;
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

int main(int argc, char **argv)
{
	Solver		*s;
	ExprBuilder	*eb;

	llvm::sys::PrintStackTraceOnErrorSignal();
	llvm::cl::ParseCommandLineOptions(argc, argv);

	eb = createExprBuilder();
	if (eb == NULL) {
		std::cerr << "[kopt] Could not create builder\n";
		return -1;
	}

	s = Solver::createChain();
	if (s == NULL) {
		std::cerr << "[kopt] Could not initialize solver\n";
		return -3;
	}

	if (CheckRule) {
		checkRule(eb, s);
	} else if (ApplyRule.size()) {
		applyRule(eb, s);
	} else if (ApplyTransitivity) {
		applyTransitivity(eb, s);
	}else {
		printRule(eb, s);
	}

	delete s;
	delete eb;
	llvm::llvm_shutdown();
	return 0;
}

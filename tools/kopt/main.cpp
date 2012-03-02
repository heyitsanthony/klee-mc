#include <iostream>

#include "../../lib/Expr/SMTParser.h"
#include "../../lib/Expr/ExprRule.h"
#include "../../lib/Expr/RuleBuilder.h"
#include "../../lib/Expr/OptBuilder.h"
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

	cl::opt<bool>
	UseBin(
		"use-bin",
		cl::desc("Use binary rule for input."),
		cl::init(false));

	cl::opt<bool>
	DumpBinRule(
		"dump-bin",
		cl::desc("Dump rule in binary format."),
		cl::init(false));

	enum BuilderKinds {
		DefaultBuilder,
		ConstantFoldingBuilder,
		SimplifyingBuilder,
		HandOptBuilder
	};

	static cl::opt<BuilderKinds>
	BuilderKind("builder",
		cl::desc("Expression builder:"),
		cl::init(SimplifyingBuilder),
		cl::values(
			clEnumValN(DefaultBuilder, "default",
			"Default expression construction."),
			clEnumValN(ConstantFoldingBuilder, "constant-folding",
			"Fold constant expressions."),
			clEnumValN(SimplifyingBuilder, "simplify",
			"Fold constants and simplify expressions."),
			clEnumValN(HandOptBuilder, "handopt",
			"Hand-optimized builder."),
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
	case HandOptBuilder:
		delete Builder;
		Builder = new OptBuilder();
		break;
	}

	return Builder;
}

static ExprRule* loadRule(const char *path)
{
	if (UseBin)
		return ExprRule::loadBinaryRule(path);
	return ExprRule::loadPrettyRule(path);
}

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

static void checkRule(ExprBuilder *eb, Solver* s)
{
	ExprRule	*er = loadRule(InputFile.c_str());
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

	if (er->getToExpr() == er->getFromExpr()) {
		std::cout << "identity rule\n";
	} else if (to_nodes >= from_nodes) {
		std::cout << "non-shrinking rule\n";
	} else if (mustBeTrue) {
		std::cout << "valid rule\n";
	} else {
		std::cout << "invalid rule\n";
	}

	delete er;
}

static void applyTransitivity(ExprBuilder* eb, Solver* s)
{
	ExprBuilder	*rule_eb, *old_eb;
	ExprRule	*er;
	ref<Expr>	rule_to_rb, rule_from_rb;
	ref<Expr>	rule_to_old, rule_from_old;
	ref<Expr>	init_expr, bridge_expr, impl_expr, new_rule_expr;
	bool		ok, mustBeTrue;

	er = loadRule(InputFile.c_str());
	assert (er != NULL && "Bad rule?");

	rule_from_old = er->getFromExpr();
	rule_to_old = er->getToExpr();

	rule_eb = new RuleBuilder(createExprBuilder());
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
	assert (ok && "Unhandled solver failure");

	if (mustBeTrue) {
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

	er = loadRule(ApplyRule.c_str());
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
	} else if (DumpBinRule) {
		ExprRule	*er = loadRule(InputFile.c_str());
		er->printBinaryRule(std::cout);
	} else {
		printRule(eb, s);
	}

	delete s;
	delete eb;
	llvm::llvm_shutdown();
	return 0;
}

#include <iostream>
#include <sstream>
#include "klee/Solver.h"
#include "klee/Constraints.h"
#include "klee/ExprBuilder.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/Assignment.h"
#include "llvm/Support/CommandLine.h"
#include "SMTPrinter.h"
#include "static/Sugar.h"
#include <queue>

using namespace klee;

namespace
{
	llvm::cl::opt<bool>
	OnlyCheckTopLevelExpr(
		"only-toplevel-xchk",
		llvm::cl::init(true));

	llvm::cl::opt<bool>
	OptimizeConstChecking(
		"opt-const-xchk",
		llvm::cl::init(true));

	llvm::cl::opt<bool>
	OptimizeEqHash("opt-ignore-eqhash", llvm::cl::init(true));

	llvm::cl::opt<bool>
	RandomValueCheck(
		"opt-rvc",
		llvm::cl::desc("Optimize exprxchk with test values (imprecise)"),
		llvm::cl::init(false));
}

//#define DEFAULT_XCHK_BUILDER	test_builder
#define DEFAULT_XCHK_BUILDER	oracle_builder

class ExprXChkBuilder : public ExprBuilder
{
public:
	static ExprXChkBuilder	*theXChkBuilder;

	ExprXChkBuilder(Solver& s, ExprBuilder* oracle, ExprBuilder* test)
	: solver(s)
	, oracle_builder(oracle)
	, test_builder(test)
	, in_xchker(false)
	{
		theXChkBuilder = this;
	}

	virtual ~ExprXChkBuilder(void)
	{
		theXChkBuilder = NULL;
		delete oracle_builder;
		delete test_builder;
	}

	// for debugging convenience
	static void xchkExpr(
		const ref<Expr>& oracle,
		const ref<Expr>& test)
	{
		assert (theXChkBuilder != NULL &&
			"Did you remember to enable -xchk-expr-builder?");
		if (theXChkBuilder->in_xchker) {
			theXChkBuilder->deferred_exprs.push(
				std::make_pair(oracle, test));
			return;
		}
		theXChkBuilder->in_xchker = true;
		theXChkBuilder->xchk(oracle, test);
	}

	virtual ref<Expr> Constant(const llvm::APInt &Value)
	{ return DEFAULT_XCHK_BUILDER->Constant(Value); }

	virtual ref<Expr> NotOptimized(const ref<Expr> &Index)
	{ return DEFAULT_XCHK_BUILDER->NotOptimized(Index); }

protected:
	void xchk(
		const ref<Expr>& oracle_expr,
		const ref<Expr>& test_expr);
	void xchkWithSolver(
		const ref<Expr>& oracle_expr,
		const ref<Expr>& test_expr);
	void dumpCounterExample(
		std::ostream& os,
		const ref<Expr>& oracle_expr,
		const ref<Expr>& test_expr);
	void printBadXChk(
		std::ostream& os,
		const Query& q,
		const ref<Expr>& oracle_expr,
		const ref<Expr>& test_expr);

	void notifyBadXChk(
		const Query& q,
		const ref<Expr>& oracle_expr,
		const ref<Expr>& test_expr);

	void xchkRandomValue(
		const ref<Expr>& oracle_expr,
		const ref<Expr>& test_expr);

	Solver		&solver;
	ExprBuilder	*oracle_builder, *test_builder;
	bool		in_xchker;
	std::queue<
		std::pair<
			ref<Expr> /* oracle */,
			ref<Expr> /* test */ > > deferred_exprs;
};

class AllExprXChkBuilder : public ExprXChkBuilder
{
public:
	AllExprXChkBuilder(Solver& s, ExprBuilder* oracle, ExprBuilder* test)
	: ExprXChkBuilder(s, oracle, test)
	{}

	virtual ~AllExprXChkBuilder() {}

	virtual ref<Expr> Read(
		const UpdateList &Updates,
		const ref<Expr> &Index)
	{
		ref<Expr>	e_test, e_oracle;

		e_test = test_builder->Read(Updates, Index);
		e_oracle = oracle_builder->Read(Updates, Index);

		in_xchker = true;
		xchk(e_oracle, e_test);
		return e_test;
	}

	virtual ref<Expr> Select(
		const ref<Expr> &Cond,
		const ref<Expr> &LHS,
		const ref<Expr> &RHS)
	{
		ref<Expr>	e_test, e_oracle;

		e_test = test_builder->Select(Cond, LHS, RHS);
		e_oracle = oracle_builder->Select(Cond, LHS, RHS);

		in_xchker = true;
		xchk(e_oracle, e_test);
		return e_test;
	}

	virtual ref<Expr> Extract(
		const ref<Expr> &LHS, unsigned Offset, Expr::Width W)
	{
		ref<Expr>	e_test, e_oracle;

		e_test = test_builder->Extract(LHS, Offset, W);
		e_oracle = oracle_builder->Extract(LHS, Offset, W);

		in_xchker = true;
		xchk(e_oracle, e_test);
		return e_test;
	}

	virtual ref<Expr> ZExt(const ref<Expr> &LHS, Expr::Width W)
	{
		ref<Expr>	e_test, e_oracle;

		e_test = test_builder->ZExt(LHS, W);
		e_oracle = oracle_builder->ZExt(LHS, W);

		in_xchker = true;
		xchk(e_oracle, e_test);
		return e_test;
	}

	virtual ref<Expr> SExt(const ref<Expr> &LHS, Expr::Width W)
	{
		ref<Expr>	e_test, e_oracle;

		e_test = test_builder->SExt(LHS, W);
		e_oracle = oracle_builder->SExt(LHS, W);

		in_xchker = true;
		xchk(e_oracle, e_test);
		return e_test;
	}

	virtual ref<Expr> Not(const ref<Expr> &L)
	{
		ref<Expr>	e_test, e_oracle;

		e_test = test_builder->Not(L);
		e_oracle = oracle_builder->Not(L);

		in_xchker = true;
		xchk(e_oracle, e_test);
		return e_test;
	}
#define DECL_BIN_REF(x)	\
virtual ref<Expr> x(const ref<Expr> &LHS, const ref<Expr> &RHS) \
{								\
	ref<Expr>	e_test, e_oracle;			\
	e_test = test_builder->x(LHS, RHS);			\
	e_oracle = oracle_builder->x(LHS, RHS);			\
	in_xchker = true;					\
	xchk(e_oracle, e_test);					\
	return e_test;						\
}

	DECL_BIN_REF(Concat)
	DECL_BIN_REF(Add)
	DECL_BIN_REF(Sub)
	DECL_BIN_REF(Mul)
	DECL_BIN_REF(UDiv)

	DECL_BIN_REF(SDiv)
	DECL_BIN_REF(URem)
	DECL_BIN_REF(SRem)
	DECL_BIN_REF(And)
	DECL_BIN_REF(Or)
	DECL_BIN_REF(Xor)
	DECL_BIN_REF(Shl)
	DECL_BIN_REF(LShr)
	DECL_BIN_REF(AShr)
	DECL_BIN_REF(Eq)
	DECL_BIN_REF(Ne)
	DECL_BIN_REF(Ult)
	DECL_BIN_REF(Ule)

	DECL_BIN_REF(Ugt)
	DECL_BIN_REF(Uge)
	DECL_BIN_REF(Slt)
	DECL_BIN_REF(Sle)
	DECL_BIN_REF(Sgt)
	DECL_BIN_REF(Sge)
#undef DECL_BIN_REF
};

class TopExprXChkBuilder : public ExprXChkBuilder
{
public:
	TopExprXChkBuilder(Solver& s, ExprBuilder* oracle, ExprBuilder* test)
	: ExprXChkBuilder(s, oracle, test)
	{}

	virtual ~TopExprXChkBuilder() {}

	virtual ref<Expr> Read(
		const UpdateList &Updates,
		const ref<Expr> &Index)
	{
		ref<Expr>	e_test, e_oracle;

		if (!in_xchker) {
			in_xchker = true;
			e_test = test_builder->Read(Updates, Index);
		} else
			return DEFAULT_XCHK_BUILDER->Read(Updates, Index);

		e_oracle = oracle_builder->Read(Updates, Index);
		xchk(e_oracle, e_test);
		return e_test;
	}

	virtual ref<Expr> Select(
		const ref<Expr> &Cond,
		const ref<Expr> &LHS,
		const ref<Expr> &RHS)
	{
		ref<Expr>	e_test, e_oracle;
		if (!in_xchker) {
			in_xchker = true;
			e_test = test_builder->Select(Cond, LHS, RHS);
		} else
			return DEFAULT_XCHK_BUILDER->Select(Cond, LHS, RHS);

		e_oracle = oracle_builder->Select(Cond, LHS, RHS);
		xchk(e_oracle, e_test);
		return e_test;
	}

	virtual ref<Expr> Extract(
		const ref<Expr> &LHS, unsigned Offset, Expr::Width W)
	{
		ref<Expr>	e_test, e_oracle;
		if (!in_xchker) {
			in_xchker = true;
			e_test = test_builder->Extract(LHS, Offset, W);
		} else
			return DEFAULT_XCHK_BUILDER->Extract(LHS, Offset, W);
		e_oracle = oracle_builder->Extract(LHS, Offset, W);
		xchk(e_oracle, e_test);
		return e_test;
	}

	virtual ref<Expr> ZExt(const ref<Expr> &LHS, Expr::Width W)
	{
		ref<Expr>	e_test, e_oracle;
		if (!in_xchker) {
			in_xchker = true;
			e_test = test_builder->ZExt(LHS, W);
		} else
			return DEFAULT_XCHK_BUILDER->ZExt(LHS, W);
		e_oracle = oracle_builder->ZExt(LHS, W);
		xchk(e_oracle, e_test);
		return e_test;
	}

	virtual ref<Expr> SExt(const ref<Expr> &LHS, Expr::Width W)
	{
		ref<Expr>	e_test, e_oracle;
		if (!in_xchker) {
			in_xchker = true;
			e_test = test_builder->SExt(LHS, W);
		} else
			return DEFAULT_XCHK_BUILDER->SExt(LHS, W);
		e_oracle = oracle_builder->SExt(LHS, W);
		xchk(e_oracle, e_test);
		return e_test;
	}

	virtual ref<Expr> Not(const ref<Expr> &L)
	{
		ref<Expr>	e_test, e_oracle;
		if (!in_xchker) {
			in_xchker = true;
			e_test = test_builder->Not(L);
		} else
			return DEFAULT_XCHK_BUILDER->Not(L);
		e_oracle = oracle_builder->Not(L);
		xchk(e_oracle, e_test);
		return e_test;
	}
#define DECL_BIN_REF(x)	\
virtual ref<Expr> x(const ref<Expr> &LHS, const ref<Expr> &RHS) \
{								\
	ref<Expr>	e_test, e_oracle;			\
	if (!in_xchker) {					\
		in_xchker = true;				\
		e_test = test_builder->x(LHS, RHS);		\
	} else							\
		return DEFAULT_XCHK_BUILDER->x(LHS, RHS);	\
	e_oracle = oracle_builder->x(LHS, RHS);			\
	xchk(e_oracle, e_test);					\
	return e_test;						\
}

	DECL_BIN_REF(Concat)
	DECL_BIN_REF(Add)
	DECL_BIN_REF(Sub)
	DECL_BIN_REF(Mul)
	DECL_BIN_REF(UDiv)

	DECL_BIN_REF(SDiv)
	DECL_BIN_REF(URem)
	DECL_BIN_REF(SRem)
	DECL_BIN_REF(And)
	DECL_BIN_REF(Or)
	DECL_BIN_REF(Xor)
	DECL_BIN_REF(Shl)
	DECL_BIN_REF(LShr)
	DECL_BIN_REF(AShr)
	DECL_BIN_REF(Eq)
	DECL_BIN_REF(Ne)
	DECL_BIN_REF(Ult)
	DECL_BIN_REF(Ule)

	DECL_BIN_REF(Ugt)
	DECL_BIN_REF(Uge)
	DECL_BIN_REF(Slt)
	DECL_BIN_REF(Sle)
	DECL_BIN_REF(Sgt)
	DECL_BIN_REF(Sge)
#undef DECL_BIN_REF
};

void ExprXChkBuilder::xchk(
	const ref<Expr>& oracle_expr,
	const ref<Expr>& test_expr)
{
	const ConstantExpr	*ce_o, *ce_t;

	assert (in_xchker);

	if (OptimizeEqHash && oracle_expr->hash() == test_expr->hash()) {
		in_xchker = false;
		return;
	}

	// expressions may simplify to constants
	// (even though we already skip Constant() calls)--
	// there's no reason to go out to the solver since
	// we can check them right here
	if (	OptimizeConstChecking &&
		(ce_o = dyn_cast<ConstantExpr>(oracle_expr)) &&
		(ce_t = dyn_cast<ConstantExpr>(test_expr)))
	{
		if (*ce_o != *ce_t) {
			ConstraintManager	cm;
			Query	q(cm, EqExpr::alloc(oracle_expr, test_expr));

			notifyBadXChk(q, oracle_expr, test_expr);
		}

		in_xchker = false;
		return;
	}

	// structural equivalence => semantic equivalence
	if (OptimizeConstChecking && oracle_expr == test_expr) {
		in_xchker = false;
		return;
	}

	// one final fast-check: try a few random assignments
	if (RandomValueCheck) {
		xchkRandomValue(oracle_expr, test_expr);
		in_xchker = false;
		return;
	}

	if (solver.inSolver()) {
		/* can't xchk while already in the solver...
		 * nasty recursion issues pop up */
		deferred_exprs.push(std::make_pair(oracle_expr, test_expr));
		in_xchker = false;
		return;
	} 

	/* xchk all deferred expressions */
	while (!deferred_exprs.empty()) {
		std::pair<ref<Expr>, ref<Expr> > p(deferred_exprs.front());
		deferred_exprs.pop();
		xchkWithSolver(p.first, p.second);
	}

	xchkWithSolver(oracle_expr, test_expr);
	in_xchker = false;
}

void ExprXChkBuilder::xchkRandomValue(
	const ref<Expr>& oracle_expr,
	const ref<Expr>& test_expr)
{
	ref<Expr>		eval,
				eq_expr(EqExpr::create(
					oracle_expr, test_expr));
	Assignment		a(eq_expr);
	const ConstantExpr	*ce;

	a.bindFreeToU8(0xa7);
	eval = a.evaluate(eq_expr);
	ce = dyn_cast<ConstantExpr>(eq_expr);
	assert (ce != NULL);

	if (ce->isZero()) {
		/* failed */
		ConstraintManager	cm;
		Query			q(
			cm, EqExpr::alloc(oracle_expr, test_expr));

		notifyBadXChk(q, oracle_expr, test_expr);
		return;
	}
}

void ExprXChkBuilder::dumpCounterExample(
	std::ostream& os,
	const ref<Expr>& oracle_expr,
	const ref<Expr>& test_expr)
{
	bool			ok;
	Query			q(EqExpr::alloc(oracle_expr, test_expr));
	Assignment		a(q.expr);
	ref<Expr>		oracle_eval, test_eval;


	ok = solver.getInitialValues(q, a);
	if (!ok) {
		os << "Could not find counter example! Buggy solver?!\n";
		return;
	}

	os << "Counter example:\n";
	a.print(os);

	oracle_eval = a.evaluate(oracle_expr);
	os << "Oracle expr: ";
	oracle_eval->print(os);
	os << '\n';

	test_eval = a.evaluate(test_expr);
	os << "Test expr: ";
	test_eval->print(os);
	os << '\n';
}

void ExprXChkBuilder::xchkWithSolver(
	const ref<Expr>& oracle_expr,
	const ref<Expr>& test_expr)
{
	bool			ok, res;
	ConstraintManager	cm;
	Query			q(cm, EqExpr::alloc(oracle_expr, test_expr));

	/* ignore if errors already detected */
	if (Expr::errors) return;

	ok = solver.mustBeTrue(q, res);
	if (!ok)
		return;

	if (res != true) {
		notifyBadXChk(q, oracle_expr, test_expr);
	}
}


void ExprXChkBuilder::notifyBadXChk(
	const Query& q,
	const ref<Expr>& oracle_expr,
	const ref<Expr>& test_expr)
{
	std::stringstream	ss;

	printBadXChk(ss, q, oracle_expr, test_expr);
	Expr::errors++;
	Expr::errorMsg = ss.str();
	Expr::errorExpr =  EqExpr::alloc(oracle_expr, test_expr);
}

void ExprXChkBuilder::printBadXChk(
	std::ostream& os,
	const Query& q,
	const ref<Expr>& oracle_expr,
	const ref<Expr>& test_expr)
{
	os << "BAD XCHK: ";
	q.expr->print(os);
	os << '\n';

	os << "ORACLE-EXPR: " << oracle_expr << '\n';
	os << "TEST-EXPR: " << test_expr << '\n';

	dumpCounterExample(os, oracle_expr, test_expr);
	SMTPrinter::dump(q, "exprxchk");
}

ExprXChkBuilder* ExprXChkBuilder::theXChkBuilder = NULL;

ExprBuilder *createXChkBuilder(
	Solver& solver,
	ExprBuilder* oracle, ExprBuilder* test)
{
	if (OnlyCheckTopLevelExpr)
		return new TopExprXChkBuilder(solver, oracle, test);
	return new AllExprXChkBuilder(solver, oracle, test);
}

void xchkExpr(const ref<Expr>& oracle, const ref<Expr>& test)
{ ExprXChkBuilder::xchkExpr(oracle, test); }

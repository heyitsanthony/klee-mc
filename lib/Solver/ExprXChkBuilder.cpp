#include <iostream>
#include "klee/Solver.h"
#include "klee/Constraints.h"
#include "klee/ExprBuilder.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/Assignment.h"
#include "SMTPrinter.h"
#include "static/Sugar.h"
#include <queue>

using namespace klee;

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
	{
		return test_builder->Constant(Value);
	}

	virtual ref<Expr> NotOptimized(const ref<Expr> &Index)
	{ return test_builder->NotOptimized(Index); }

	virtual ref<Expr> Read(
		const UpdateList &Updates,
		const ref<Expr> &Index)
	{
		ref<Expr>	e_test, e_oracle;

		if (!in_xchker) {
			in_xchker = true;
			e_test = test_builder->Read(Updates, Index);
		} else
			return test_builder->Read(Updates, Index);

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
			return test_builder->Select(Cond, LHS, RHS);

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
			return test_builder->Extract(LHS, Offset, W);
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
			return test_builder->ZExt(LHS, W);
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
			return test_builder->SExt(LHS, W);
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
			return test_builder->Not(L);
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
		return test_builder->x(LHS, RHS);		\
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
private:
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

	Solver		&solver;
	ExprBuilder	*oracle_builder, *test_builder;
	bool		in_xchker;
	std::queue<
		std::pair<
			ref<Expr> /* oracle */,
			ref<Expr> /* test */ > > deferred_exprs;
};

void ExprXChkBuilder::xchk(
	const ref<Expr>& oracle_expr,
	const ref<Expr>& test_expr)
{
	const ConstantExpr	*ce_o, *ce_t;

	assert (in_xchker);

	// expressions may simplify to constants
	// (even though we already skip Constant() calls)--
	// there's no reason to go out to the solver since
	// we can check them right here
	if (	(ce_o = dyn_cast<ConstantExpr>(oracle_expr)) &&
		(ce_t = dyn_cast<ConstantExpr>(test_expr)))
	{
		assert (oracle_expr == test_expr && "XCHK: CE MISMATCH");
		in_xchker = false;
		return;
	}

	// structural equivalence => semantic equivalence
	if (oracle_expr == test_expr) {
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

void ExprXChkBuilder::dumpCounterExample(
	std::ostream& os,
	const ref<Expr>& oracle_expr,
	const ref<Expr>& test_expr)
{
	std::vector<const Array*>			objects;
	std::vector< std::vector<unsigned char> >	values;
	bool			ok;
	ConstraintManager	cm;
	Query			q(cm, EqExpr::alloc(oracle_expr, test_expr));
	Assignment		*bindings;


	findSymbolicObjects(q.expr, objects);
	ok = solver.getInitialValues(q, objects, values);

	if (!ok) {
		os << "Could not find counter example! Buggy solver?!\n";
		return;
	}

	os << "Counter example:\n";
	for (unsigned i = 0; i < objects.size(); i++) {
		os << objects[i]->name << ": ";
		foreach (it, values[i].begin(), values[i].end())
			os << ((void*)(*it)) << ' ';
		os << '\n';
	}

	bindings = new Assignment(objects, values);
	os << "Oracle expr: ";
	bindings->evaluate(oracle_expr)->print(os);
	os << '\n';

	os << "Test expr: ";
	bindings->evaluate(test_expr)->print(os);
	os << '\n';

	delete bindings;
}

void ExprXChkBuilder::xchkWithSolver(
	const ref<Expr>& oracle_expr,
	const ref<Expr>& test_expr)
{
	bool			ok, res;
	ConstraintManager	cm;
	Query			q(cm, EqExpr::alloc(oracle_expr, test_expr));

	ok = solver.mustBeTrue(q, res);
	if (!ok)
		return;

	if (res != true) {
		std::cerr << "BAD XCHK: ";
		q.expr->print(std::cerr);
		std::cerr << '\n';

		dumpCounterExample(std::cerr, oracle_expr, test_expr);
		SMTPrinter::dump(q, "exprxchk");
	}
	assert (res == true && "XCHK FAILED! MUST BE EQUAL!");
}

ExprXChkBuilder* ExprXChkBuilder::theXChkBuilder = NULL;

ExprBuilder *createXChkBuilder(
	Solver& solver,
	ExprBuilder* oracle, ExprBuilder* test)
{ return new ExprXChkBuilder(solver, oracle, test); }

void xchkExpr(const ref<Expr>& oracle, const ref<Expr>& test)
{
	ExprXChkBuilder::xchkExpr(oracle, test);
}

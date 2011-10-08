#include <iostream>
#include "klee/Solver.h"
#include "klee/Constraints.h"
#include "klee/ExprBuilder.h"

using namespace klee;

class ExprXChkBuilder : public ExprBuilder
{
public:
	ExprXChkBuilder(Solver& s, ExprBuilder* oracle, ExprBuilder* test)
	: solver(s)
	, oracle_builder(oracle)
	, test_builder(test)
	, in_xchker(false)
	{}

	virtual ~ExprXChkBuilder(void)
	{
		delete oracle_builder;
		delete test_builder;
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
	void		xchk(
		const ref<Expr>& oracle_expr,
		const ref<Expr>& test_expr);
	Solver		&solver;
	ExprBuilder	*oracle_builder, *test_builder;
	bool		in_xchker;
};

void ExprXChkBuilder::xchk(
	const ref<Expr>& oracle_expr,
	const ref<Expr>& test_expr)
{
	bool			ok, res;
	ConstraintManager	cm;
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

	Query			q(cm, EqExpr::alloc(oracle_expr, test_expr));
	std::cerr << "XCHKING: ";
	q.expr->print(std::cerr);
	std::cerr << '\n';
	ok = solver.mustBeTrue(q, res);
	in_xchker = false;
	if (!ok)
		return;

	assert (res == true && "XCHK FAILED! MUST BE EQUAL!");
}

ExprBuilder *createXChkBuilder(
	Solver& solver,
	ExprBuilder* oracle, ExprBuilder* test)
{ return new ExprXChkBuilder(solver, oracle, test); }

#include <iostream>
#include "klee/Solver.h"
#include "klee/Constraints.h"
#include "klee/ExprBuilder.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/Assignment.h"
#include "llvm/Support/CommandLine.h"
#include "SMTPrinter.h"
#include "static/Sugar.h"

using namespace klee;

class EquivExprBuilder : public ExprBuilder
{
public:
	EquivExprBuilder(Solver& s, ExprBuilder* in_eb)
	: solver(s)
	, eb(in_eb)
	, depth(0)
	{}

	virtual ~EquivExprBuilder(void) { delete eb; }

	virtual ref<Expr> Constant(const llvm::APInt &Value)
	{ return eb->Constant(Value); }

	virtual ref<Expr> NotOptimized(const ref<Expr> &Index)
	{ return eb->NotOptimized(Index); }

	ref<Expr> Read(const UpdateList &u, const ref<Expr> &i)
	{
		ref<Expr>	ret(eb->Read(u, i));
		return lookup(ret);
	}

	ref<Expr> Select(
		const ref<Expr> &Cond,
		const ref<Expr> &LHS, const ref<Expr> &RHS)
	{
		ref<Expr>	ret(eb->Select(Cond, LHS, RHS));
		return lookup(ret);
	}

	ref<Expr> Extract(const ref<Expr> &LHS, unsigned Offset, Expr::Width W)
	{
		ref<Expr>	ret(eb->Extract(LHS, Offset, W));
		return lookup(ret);
	}

	ref<Expr> ZExt(const ref<Expr> &LHS, Expr::Width W)
	{
		ref<Expr>	ret(eb->ZExt(LHS, W));
		return lookup(ret);
	}

	ref<Expr> SExt(const ref<Expr> &LHS, Expr::Width W)
	{
		ref<Expr>	ret(eb->SExt(LHS, W));
		return lookup(ret);
	}

	ref<Expr> Not(const ref<Expr> &LHS)
	{
		ref<Expr>	ret(eb->Not(LHS));
		return lookup(ret);
	}

#define DECL_BIN_REF(x)	\
ref<Expr> x(const ref<Expr> &LHS, const ref<Expr> &RHS) \
{ \
ref<Expr> ret(eb->x(LHS, RHS));	\
return lookup(ret); }	
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


protected:
	ref<Expr> lookup(ref<Expr>& e);

	unsigned int	depth;
	Solver		&solver;
	ExprBuilder	*eb;
};


ref<Expr> EquivExprBuilder::lookup(ref<Expr>& e)
{
	assert (0 == 1 && "STUB");
}

ExprBuilder *createEquivBuilder(Solver& solver, ExprBuilder* eb)
{ return new EquivExprBuilder(solver, eb); }

#ifndef KLEE_EXPRALLOC_H
#define KLEE_EXPRALLOC_H

#include "klee/ExprBuilder.h"

namespace klee
{
/// ExprBuilder - Base expression builder class.
class ExprAlloc : public ExprBuilder
{
public:
	ExprAlloc() {}
	virtual ~ExprAlloc();

	virtual int compare(const Expr& lhs, const Expr& rhs)
	{ return lhs.compareDeep(rhs); }

	// Expressions
	virtual ref<Expr> Constant(const llvm::APInt &Value);
	virtual ref<Expr> NotOptimized(const ref<Expr> &Index);
	virtual ref<Expr> Read(const UpdateList &Updates, const ref<Expr> &idx);
	virtual ref<Expr> Select(
		const ref<Expr> &Cond,
		const ref<Expr> &LHS, const ref<Expr> &RHS);
	virtual ref<Expr> Extract(
		const ref<Expr> &LHS, unsigned Offset, Expr::Width W);
	virtual ref<Expr> ZExt(const ref<Expr> &LHS, Expr::Width W);
	virtual ref<Expr> SExt(const ref<Expr> &LHS, Expr::Width W);

	virtual ref<Expr> Not(const ref<Expr> &LHS);
#define DECL_BIN_REF(x)	\
virtual ref<Expr> x(const ref<Expr> &LHS, const ref<Expr> &RHS);
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

	static unsigned long getNumConstants(void) { return constantCount; }
	virtual unsigned garbageCollect(void);
private:
	static unsigned long constantCount;
};
}

#endif

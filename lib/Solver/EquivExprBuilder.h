#ifndef EQUIVEXPRBUILDER_H
#define EQUIVEXPRBUILDER_H

#include "klee/Solver.h"
#include "klee/ExprBuilder.h"
#include <tr1/unordered_map>
#include <tr1/unordered_set>


#define EE_MIN_NODES	2
#define EE_MAX_NODES	40

namespace klee
{

class EquivExprBuilder : public ExprBuilder
{
public:
	EquivExprBuilder(Solver& s, ExprBuilder* in_eb);

	virtual ~EquivExprBuilder(void);

	virtual ref<Expr> Constant(const llvm::APInt &Value)
	{ return eb->Constant(Value); }

	virtual ref<Expr> NotOptimized(const ref<Expr> &Index)
	{ return eb->NotOptimized(Index); }

	ref<Expr> Read(const UpdateList &u, const ref<Expr> &i)
	{
		depth++;
		ref<Expr>	ret(eb->Read(u, i));
		return lookup(ret);
	}

	ref<Expr> Select(
		const ref<Expr> &Cond,
		const ref<Expr> &LHS, const ref<Expr> &RHS)
	{
		depth++;
		ref<Expr>	ret(eb->Select(Cond, LHS, RHS));
		return lookup(ret);
	}

	ref<Expr> Extract(const ref<Expr> &LHS, unsigned Offset, Expr::Width W)
	{
		depth++;
		ref<Expr>	ret(eb->Extract(LHS, Offset, W));
		return lookup(ret);
	}

	ref<Expr> ZExt(const ref<Expr> &LHS, Expr::Width W)
	{
		depth++;
		ref<Expr>	ret(eb->ZExt(LHS, W));
		return lookup(ret);
	}

	ref<Expr> SExt(const ref<Expr> &LHS, Expr::Width W)
	{
		depth++;
		ref<Expr>	ret(eb->SExt(LHS, W));
		return lookup(ret);
	}

	ref<Expr> Not(const ref<Expr> &LHS)
	{
		depth++;
		ref<Expr>	ret(eb->Not(LHS));
		return lookup(ret);
	}

#define DECL_BIN_REF(x)	\
ref<Expr> x(const ref<Expr> &LHS, const ref<Expr> &RHS) \
{ \
depth++;\
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
	ref<Expr> lookupByEval(ref<Expr>& e, unsigned nodes);
	uint64_t getEvalHash(ref<Expr>& e, bool &maybeConst);
	uint64_t getEvalHash(ref<Expr>& e)
	{
		bool mc;
		return getEvalHash(e, mc);
	}

	ref<Expr> tryEquivRewrite(
		const ref<Expr>& e, const ref<Expr>& smaller);

	unsigned int			depth;
	Solver				&solver;
	ExprBuilder			*eb;	/* default builder */
	std::vector<uint8_t>	sample_seq;
	std::vector<uint8_t>	sample_seq_onoff;
	std::vector<uint8_t>	sample_nonseq_zeros[8];

	uint64_t			served_c;
	uint64_t			ign_c;
	uint64_t			const_c;
	uint64_t			wide_c;
	typedef std::tr1::unordered_set<uint64_t> constset_ty;
	typedef std::tr1::unordered_map<uint64_t, uint64_t> constmap_ty;
	constmap_ty			consts_map;
	constset_ty			consts;
};

}

#endif

#ifndef RULEBUILDER_H
#define RULEBUILDER_H

#include "klee/ExprBuilder.h"

namespace klee
{
class ExprRule;

class RuleBuilder : public ExprBuilder
{
public:
	RuleBuilder(ExprBuilder* base);
	virtual ~RuleBuilder(void);

	virtual ref<Expr> Constant(const llvm::APInt &Value)
	{ return ConstantExpr::alloc(Value); }

	virtual ref<Expr> NotOptimized(const ref<Expr> &Index)
	{ return NotOptimizedExpr::alloc(Index); }

#define APPLY_RULE_HDR			\
	ref<Expr>	eb_e, ret;	\
	depth++;			\

#define APPLY_RULE_FTR		\
	depth--;		\
	if (depth == 0) {	\
		ret = tryApplyRules(eb_e);	\
		if (ret.isNull())	\
			ret = eb_e;	\
	} else				\
		ret = eb_e;		\
	return ret;

	virtual ref<Expr> Not(const ref<Expr> &L)
	{
		APPLY_RULE_HDR
		eb_e = eb->Not(L);
		APPLY_RULE_FTR
	}

	virtual ref<Expr> Read(
		const UpdateList &Updates,
		const ref<Expr> &Index)
	{
		APPLY_RULE_HDR
		eb_e = eb->Read(Updates, Index);
		APPLY_RULE_FTR
	}

	virtual ref<Expr> Select(
		const ref<Expr> &Cond,
		const ref<Expr> &LHS,
		const ref<Expr> &RHS)
	{
		APPLY_RULE_HDR
		eb_e = eb->Select(Cond, LHS, RHS);
		APPLY_RULE_FTR
	}

	virtual ref<Expr> Extract(
		const ref<Expr> &LHS,
		unsigned Offset,
		Expr::Width W)
	{
		APPLY_RULE_HDR
		eb_e = eb->Extract(LHS, Offset, W);
		APPLY_RULE_FTR
	}

	ref<Expr> Ne(const ref<Expr> &l, const ref<Expr> &r)
	{
		APPLY_RULE_HDR
		eb_e = EqExpr::create(
			ConstantExpr::create(0, Expr::Bool),
			EqExpr::create(l, r));
		APPLY_RULE_FTR
	}

	ref<Expr> Ugt(const ref<Expr> &l, const ref<Expr> &r)
	{ return UltExpr::create(r, l); }

	ref<Expr> Uge(const ref<Expr> &l, const ref<Expr> &r)
	{ return UleExpr::create(r, l); }

	ref<Expr> Sgt(const ref<Expr> &l, const ref<Expr> &r)
	{ return SltExpr::create(r, l); }

	ref<Expr> Sge(const ref<Expr> &l, const ref<Expr> &r)
	{ return SleExpr::create(r, l); }

	virtual ref<Expr> ZExt(const ref<Expr> &LHS, Expr::Width W)
	{
		APPLY_RULE_HDR
		eb_e = eb->ZExt(LHS, W);
		APPLY_RULE_FTR
	}

	virtual ref<Expr> SExt(const ref<Expr> &LHS, Expr::Width W)
	{
		APPLY_RULE_HDR
		eb_e = eb->SExt(LHS, W);
		APPLY_RULE_FTR
	}


#define DECL_BIN_REF(x)						\
virtual ref<Expr> x(const ref<Expr> &LHS, const ref<Expr> &RHS)	\
{\
	APPLY_RULE_HDR		\
	eb_e = eb->x(LHS, RHS);	\
	APPLY_RULE_FTR		\
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
	DECL_BIN_REF(Ult)
	DECL_BIN_REF(Ule)

	DECL_BIN_REF(Slt)
	DECL_BIN_REF(Sle)
#undef DECL_BIN_REF
	static uint64_t getHits(void) { return hit_c; }
	static uint64_t getMisses(void) { return miss_c; }
	static uint64_t getRuleMisses(void) { return rule_miss_c; }
private:
	void loadRules(const char* ruledir);
	ref<Expr> tryApplyRules(const ref<Expr>& in);

	std::vector<ExprRule*>	rules;
	ExprBuilder		*eb;
	unsigned		depth;

	static uint64_t		hit_c;
	static uint64_t		miss_c;
	static uint64_t		rule_miss_c;
};
}

#endif

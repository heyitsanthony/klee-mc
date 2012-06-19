#ifndef RULEBUILDER_H
#define RULEBUILDER_H

#include <tr1/unordered_set>
#include <tr1/unordered_map>
#include <list>
#include "static/Trie.h"
#include "klee/ExprBuilder.h"

namespace klee
{
class ExprRule;

class RuleBuilder : public ExprBuilder
{
public:
	typedef Trie<uint64_t, ExprRule*>	ruletrie_ty;

	typedef std::list<const ExprRule*>	rtlist_ty;
	typedef std::tr1::unordered_map<
		Expr::Hash, rtlist_ty*>		ruletab_ty;

	typedef std::vector<ExprRule*>		rulearr_ty;

	RuleBuilder(ExprBuilder* base);
	virtual ~RuleBuilder(void);

	virtual ref<Expr> Constant(const llvm::APInt &Value)
	{ return ConstantExpr::alloc(Value); }

	virtual ref<Expr> NotOptimized(const ref<Expr> &Index)
	{ return NotOptimizedExpr::alloc(Index); }

#define APPLY_RULE_HDR			\
	ref<Expr>	eb_e, ret;	\
	depth++;			\

#define APPLY_RULE_FTR	\
	depth--;		\
	if (depth == 0) {	\
		ret = tryApplyRules(eb_e);	\
		if (ret.isNull())	\
			ret = eb_e;	\
	} else				\
		ret = eb_e;		\
	return ret;

#define X_APPLY_RULE_FTR	\
	depth--;		\
	ret = tryApplyRules(eb_e);	\
	if (ret.isNull())	\
		ret = eb_e;	\
	return ret;		\


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
		// NOTE: Are reads  always optimal since we slot by reads?
		// E.g. is there a reason to tryApplyRules on this?
		return eb->Read(Updates, Index);
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
	static uint64_t getFiltered(void) { return miss_filtered_c; }
	static uint64_t getNumRulesUsed(void) { return rules_used.size(); }
	static uint64_t getFilterSize(void) { return filter_size; }
	static const ExprRule* getLastRule(void) { return last_er; }

	static bool hasRule(const char* fname);

	rulearr_ty::const_iterator begin(void) const
	{ return rules_arr.begin(); }
	rulearr_ty::const_iterator end(void) const { return rules_arr.end(); }
	unsigned size(void) const { return rules_arr.size(); }

	void eraseDBRule(const ExprRule*);

	const std::string& getDBPath(void) const;

	void addRule(ExprRule* er);

private:
	void loadRules(void);
	bool loadRuleDir(const char* ruledir);
	bool loadRuleDB(const char* rulefile);

	void addRuleHash(const ExprRule* er);
	void updateLastRule(const ExprRule* er);

	bool eraseDBRuleHint(std::fstream& ifs, const ExprRule* to_rmv);
	bool tryEraseDBRule(
		std::fstream& ifs,
		std::streampos& last_pos,
		const ExprRule* to_rmv);


	ref<Expr> tryApplyRules(const ref<Expr>& in);
	ref<Expr> tryAllRules(const ref<Expr>& in);
	ref<Expr> tryTrieRules(const ref<Expr>& in);
	ref<Expr> trySkeletalRules(const ref<Expr>& in);

	ruletrie_ty		rules_trie;
	std::vector<ExprRule*>	rules_arr;

	/* skeletal hash lookup */
	/* XXX: this is missing buckets for collisions;
	 * this will miss a lot of rules in the same equiv class */
	ruletab_ty		rules_tab;

	ExprBuilder		*eb;
	unsigned		depth;
	unsigned		recur;

	static uint64_t		hit_c;
	static uint64_t		miss_c;
	static uint64_t		rule_miss_c;
	static uint64_t		miss_filtered_c;
	static uint64_t		filter_size;
	static const ExprRule	*last_er;

	static std::set<const ExprRule*>	rules_used;
	std::tr1::unordered_set<Expr::Hash>	miss_filter;
	std::ofstream		*rule_ofs;

};
}

#endif

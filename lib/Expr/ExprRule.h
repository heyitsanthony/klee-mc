#ifndef EXPRRULE_H
#define EXPRRULE_H

#include <fstream>
#include <vector>
#include "klee/Expr.h"
#include "klee/util/ExprTag.h"
#include "Pattern.h"
#include "ExprPatternMatch.h"

#define hdr_mask(x)	((x) & ~((uint64_t)0xff))
#define is_hdr_magic(x)	(hdr_mask(x) == ER_HDR_MAGIC_MASK)
#define ER_HDR_MAGIC_MASK	0xFEFEFEFE01010100
#define ER_HDR_MAGIC1		0xFEFEFEFE01010101	/* fixed consts */
#define ER_HDR_MAGIC2		0xFEFEFEFE01010102	/* constrained consts */
#define ER_HDR_SKIP		0xFEFEFEFEFEFEFEFE


namespace klee
{
class ExprRule
{
public:
	static ExprRule* loadRule(const char* fname);
	static ExprRule* loadPrettyRule(const char* fname);
	static ExprRule* loadPrettyRule(std::istream& is);
	static ExprRule* loadBinaryRule(const char* fname);
	static ExprRule* loadBinaryRule(std::istream& is);
	static ExprRule* changeDest(const ExprRule* er, const ref<Expr>& to);

	/* assumes unique variables for every repl */
	ExprRule* addConstraints(
		const exprtags_ty		&visit_tags,
		const std::vector<ref<Expr> >	&constraints) const;

	ExprRule* markUnbound(int tag) const;

	static ExprRule* createRule(
		const ref<Expr>& lhs, const ref<Expr>& rhs);
	static void printRule(
		std::ostream& os, const ref<Expr>& lhs, const ref<Expr>& rhs);
	static void printBinaryPattern(std::ostream& os, const Pattern& p);
	static void printTombstone(std::ostream& os, unsigned range_len);
	static void printExpr(std::ostream& os, const ref<Expr>& e);
	static void printExpr(
		std::ostream& os, const ref<Expr>& e, const labelmap_ty& tm);
	static void printConstr(std::ostream& os, const Pattern& p);

	virtual ~ExprRule();

	void printBinaryRule(std::ostream& os) const;
	void printPrettyRule(std::ostream& os) const;
	void print(std::ostream& os, bool use_parens = false) const;

	ref<Expr> materialize(void) const;
	ref<Expr> materializeUnsound(void) const;
	ref<Expr> getFromExpr(void) const { return from.anonFlat2Expr(); }
	ref<Expr> getToExpr(void) const	{ return to.anonFlat2Expr(); }

	ref<Expr> getCleanFromExpr(void) const
	{ return from.anonFlat2Expr(-1,true); }
	ref<Expr> getCleanToExpr(void) const
	{ return to.anonFlat2Expr(-1,true); }


	ref<Expr> apply(const ref<Expr>& e) const;
	static ref<Expr> apply(
		const ref<Expr>& e,
		ExprPatternMatch::RuleIterator& ri);


	/* XXX these are wrong-- should be taking a count from the exprs
	 * the flatrule_ty values don't represent the actual node counts */
	unsigned getFromNodeCount(void) const { return from.rule.size(); }
	unsigned getToNodeCount(void) const { return to.rule.size(); }

	const flatrule_ty& getFromKey(void) const { return from.rule; }
	const flatrule_ty& getToKey(void) const { return to.rule; }
	const Pattern& getFromPattern(void) const { return from; }
	const Pattern& getToPattern(void) const { return to; }

	virtual bool operator==(const ExprRule& er) const;
	virtual bool operator<(const ExprRule& er) const;
	bool operator!=(const ExprRule& er) const { return !(*this == er); }

	virtual ref<Array> getMaterializeArray(void) const
	{ return Pattern::getMaterializeArray(); }

	unsigned int getOffsetHint(void) const { return off_hint; }

	bool checkConstants(const labelmap_ty& clm) const;
	bool hasConstraints(void) const { return const_constraints != NULL; }
	bool hasFree(void) const;
	bool isNaive(void) const { return !hasConstraints() && !hasFree(); }

	uint64_t hash(void) const;
protected:
	ExprRule(
		const Pattern& _from,
		const Pattern& _to,
		const std::vector<Pattern>* constrs = NULL);
private:
	static bool loadBinaryPattern(std::istream& is, Pattern& p);


	static bool readFlatExpr(std::istream& ifs, Pattern& p);

	Pattern			from, to;
	std::vector<Pattern>	*const_constraints;

	mutable unsigned	apply_hit_c, apply_fail_c;
	unsigned int		off_hint;

	ExprRule(const ExprRule& er);
};

#include <set>
struct er_deref_lt
{ bool operator() (const ExprRule* a, const ExprRule* b) const { return (*a < *b); } };

struct er_deref_eq
{ bool operator() (const ExprRule* a, const ExprRule* b) const { return (*a == *b); } };

typedef std::set<const ExprRule*, struct er_deref_lt> ExprRuleSet;

}

#endif

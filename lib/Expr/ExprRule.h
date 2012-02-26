#ifndef EXPRRULE_H
#define EXPRRULE_H

#include <fstream>
#include <vector>
#include "klee/Expr.h"

#define OP_LABEL_MASK		(1ULL << 63)
#define OP_LABEL_TEST(x)	(((x) & OP_LABEL_MASK) != 0)
#define OP_LABEL_MK(x)		((x) | (1ULL << 63))
#define OP_LABEL_NUM(x)		((x) & ~(1ULL << 63))


namespace klee
{

class ExprRule
{
public:
	typedef std::vector<uint64_t>	flatrule_ty;
	typedef std::map<unsigned, ref<Expr> > labelmap_ty;

	/* this is so that we can get rule application to works for both
	 * tries and a single rule */
	class RuleIterator
	{
	public:
		virtual bool isDone(void) const = 0;
		virtual void reset(void) = 0;
		virtual bool matchValue(uint64_t v) = 0;
		virtual bool matchLabel(uint64_t& v) = 0;
		virtual const flatrule_ty& getToRule(void) const = 0;
		virtual ~RuleIterator() {}
	protected:
		RuleIterator() {}
	private:
	};

	static ExprRule* loadPrettyRule(const char* fname);
	static ExprRule* loadBinaryRule(const char* fname);
	static ExprRule* loadBinaryRule(std::istream& is);

	static void printRule(
		std::ostream& os, const ref<Expr>& lhs, const ref<Expr>& rhs);

	virtual ~ExprRule() {}

	void printBinaryRule(std::ostream& os) const;
	ref<Expr> materialize(void) const;
	ref<Expr> getFromExpr(void) const
	{ return anonFlat2Expr(from); }

	ref<Expr> getToExpr(void) const
	{ return anonFlat2Expr(to); }

	ref<Expr> apply(const ref<Expr>& e) const;
	static ref<Expr> apply(const ref<Expr>& e, RuleIterator& ri);


	/* XXX these are wrong-- should be taking a count from the exprs
	 * the flatrule_ty values don't represent the actual node counts */
	unsigned getFromNodeCount(void) const { return from.rule.size(); }
	unsigned getToNodeCount(void) const { return to.rule.size(); }

	unsigned getToLabels(void) const { return to.label_c; }
	unsigned getFromLabels(void) const { return from.label_c; }

	const flatrule_ty& getFromKey(void) const { return from.rule; }
	const flatrule_ty& getToKey(void) const { return to.rule; }

protected:
	struct Pattern {
		flatrule_ty	rule;
		unsigned	label_c;
		unsigned	label_id_max;
	};
	ExprRule(const Pattern& _from, const Pattern& _to);

private:
	static ref<Expr> flat2expr(
		const labelmap_ty& lm,
		const flatrule_ty& fr, int& off);
	ref<Expr> anonFlat2Expr(const Pattern& p) const;


	static bool readFlatExpr(std::ifstream& ifs, Pattern& p);

	Pattern		from, to;

	mutable unsigned apply_hit_c, apply_fail_c;
	ref<Array>	materialize_arr;
};

}

#endif

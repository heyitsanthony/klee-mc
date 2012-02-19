#ifndef EXPRRULE_H
#define EXPRRULE_H

#include <fstream>
#include <vector>
#include "klee/Expr.h"

#define OP_LABEL_TEST(x)	(((x) & (1ULL << 63)) != 0)
#define OP_LABEL_MK(x)		((x) | (1ULL << 63))
#define OP_LABEL_NUM(x)		((x) & ~(1ULL << 63))


namespace klee
{

class ExprRule
{
public:
	typedef std::vector<uint64_t>	flatrule_ty;
	typedef std::map<unsigned, ref<Expr> > labelmap_ty;

	static ExprRule* loadPrettyRule(const char* fname);
	static ExprRule* loadBinaryRule(const char* fname);
	static ExprRule* loadBinaryRule(std::istream& is);

	static void printRule(
		std::ostream& os, const ref<Expr>& lhs, const ref<Expr>& rhs);

	virtual ~ExprRule() {}

	void printBinaryRule(std::ostream& os) const;
	ref<Expr> materialize(void) const;
	ref<Expr> apply(const ref<Expr>& e) const;

	/* XXX these are wrong-- should be taking a count from the exprs
	 * the flatrule_ty values don't represent the actual node counts */
	unsigned getFromNodeCount(void) const { return from.rule.size(); }
	unsigned getToNodeCount(void) const { return to.rule.size(); }

	unsigned getToLabels(void) const { return to.label_c; }
	unsigned getFromLabels(void) const { return from.label_c; }

protected:
	struct Pattern {
		flatrule_ty	rule;
		unsigned	label_c;
		unsigned	label_id_max;
	};
	ExprRule(const Pattern& _from, const Pattern& _to);

private:
	ref<Expr> flat2expr(
		const labelmap_ty& lm,
		const flatrule_ty& fr, int& off) const;
	ref<Expr> anonFlat2Expr(const Array* arr, const Pattern& p) const;


	static bool readFlatExpr(std::ifstream& ifs, Pattern& p);

	Pattern		from, to;

	mutable unsigned apply_hit_c, apply_fail_c;
	ref<Array>	materialize_arr;
};

}

#endif

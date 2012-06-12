#ifndef PATTERN_H
#define PATTERN_H

#include "klee/Expr.h"
#include <vector>
#include <map>

#define OP_LABEL_MASK		(1ULL << 63)
#define OP_CLABEL_MASK		((1ULL << 63) | (1ULL << 62))

#define OP_L_TEST(x,y)		(((x) & OP_CLABEL_MASK) == y)
#define OP_L_NUM(x,y)		((x) & ~(y))

#define OP_LABEL_TEST(x)	OP_L_TEST(x, OP_LABEL_MASK)
#define OP_LABEL_NUM(x)		OP_L_NUM(x, OP_LABEL_MASK)
#define OP_LABEL_MK(x)		((x) | (OP_LABEL_MASK))

#define OP_CLABEL_TEST(x)	OP_L_TEST(x, OP_CLABEL_MASK)
#define OP_CLABEL_NUM(x)	OP_L_NUM(x, OP_CLABEL_MASK)
#define OP_CLABEL_MK(x)		((x) | OP_CLABEL_MASK)


namespace klee
{
typedef uint64_t flatrule_tok;
typedef std::vector<flatrule_tok>	flatrule_ty;
typedef std::map<unsigned, ref<Expr> > labelmap_ty;
class Pattern
{
public:
	Pattern()
	: label_c(0), label_id_max(0), clabel_c(0) {}
	~Pattern() {}

	bool operator ==(const Pattern& p) const;
	bool operator !=(const Pattern& p) const
	{ return !(*this == p); }

	ref<Expr> anonFlat2Expr(int label_max = -1) const;

	static ref<Array> getMaterializeArray(void);

	static ref<Expr> flat2expr(
		const labelmap_ty&	lm,
		const flatrule_ty&	fr,
		int&			off);

	ref<Expr> flat2expr(const labelmap_ty &lm) const
	{
		int off = 0;
		return flat2expr(lm, rule, off);
	}

	bool readFlatExpr(std::istream& ifs);
	flatrule_ty stripConstExamples(void) const;
	bool isConst(void) const;

	unsigned size(void) const { return rule.size(); }

	flatrule_ty		rule;
	uint16_t		label_c;
	uint16_t		label_id_max;
	uint16_t		clabel_c;
private:
	static ref<Array>	materialize_arr;
};

}

#endif

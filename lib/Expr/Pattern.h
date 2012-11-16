#ifndef PATTERN_H
#define PATTERN_H

#include "klee/Expr.h"
#include <vector>
#include <map>

#define OP_LABEL_MASK		(1ULL << 63)
#define OP_CLABEL_MASK		((1ULL << 63) | (1ULL << 62))
#define OP_EXT_VAR		(1ULL << 62) /* followed by 64-bit width token */
#define OP_MASK			((1ULL << 63) | (1ULL << 62))

#define OP_L_TEST(x,y)		(((x) & OP_MASK) == y)
#define OP_L_NUM(x,y)		((x) & ~(y))

#define OP_LABEL_TEST(x)	OP_L_TEST(x, OP_LABEL_MASK)
#define OP_LABEL_NUM(x)		OP_L_NUM(x, OP_LABEL_MASK)
#define OP_LABEL_MK(x)		((x) | (OP_LABEL_MASK))

#define OP_CLABEL_TEST(x)	OP_L_TEST(x, OP_CLABEL_MASK)
#define OP_CLABEL_NUM(x)	OP_L_NUM(x, OP_CLABEL_MASK)
#define OP_CLABEL_MK(x)		((x) | OP_CLABEL_MASK)

#define OP_VAR_TEST(x)		OP_L_TEST(x, OP_EXT_VAR)
#define OP_VAR_MK(x)		(OP_EXT_VAR | x)
#define OP_VAR_W(x)		((x) & ~OP_EXT_VAR)

namespace klee
{
typedef uint64_t flatrule_tok;
typedef std::vector<flatrule_tok>	flatrule_ty;
typedef std::map<unsigned, ref<Expr> > labelmap_ty;
typedef std::map<unsigned, ref<Expr> > clabelmap_ty;
class Pattern
{
public:
	Pattern()
	: label_c(0), label_id_max(0), clabel_c(0) {}
	~Pattern() {}

	bool operator ==(const Pattern& p) const;
	bool operator !=(const Pattern& p) const
	{ return !(*this == p); }
	bool operator <(const Pattern& p) const;

	ref<Expr> anonFlat2Expr(int label_max = -1, bool strip_notopt=false) const;
	ref<Expr> anonFlat2ConstrExpr(clabelmap_ty& cmap) const;
	ref<Expr> anonFlat2ConstrExpr(void) const;

	static ref<Array> getMaterializeArray(void);
	static ref<Array> getFreeArray(void);
	static ref<Array> getCLabelArray(void);

	static ref<Expr> flat2expr(
		const labelmap_ty&	lm,
		const flatrule_ty&	fr,
		int&			off,
		bool			strip_notopt = false);

	ref<Expr> flat2expr(const labelmap_ty &lm) const
	{
		int off = 0;
		return flat2expr(lm, rule, off);
	}

	void getLabelMap(labelmap_ty& lm, unsigned l_max) const;

	bool readFlatExpr(std::istream& ifs);
	flatrule_ty stripConstExamples(void) const;
	bool isConst(void) const;

	unsigned size(void) const { return rule.size(); }
	void dump(std::ostream& os) const;

	flatrule_ty		rule;
	uint16_t		label_c;
	uint16_t		label_id_max;
	uint16_t		clabel_c;
private:
	static ref<Array>	materialize_arr;
	static ref<Array>	free_arr;
	static ref<Array>	clabel_arr;
	static unsigned free_off;

};

}

#endif

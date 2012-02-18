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

	static void printPattern(
		std::ostream& os, const ref<Expr>& e);
	virtual ~ExprRule() {}

	void printBinaryRule(std::ostream& os) const;
	ref<Expr> materialize(void) const;
	ref<Expr> apply(const ref<Expr>& e) const;

protected:
	ExprRule(void);
private:
	unsigned estNumLabels(const flatrule_ty& fr) const;
	ref<Expr> flat2expr(
		const labelmap_ty& lm,
		const flatrule_ty& fr, int& off) const;
	ref<Expr> anonFlat2Expr(
		const Array* arr,
		const flatrule_ty& fr) const;


	static bool readFlatExpr(std::ifstream& ifs, flatrule_ty& r);
	flatrule_ty	from, to;

	mutable unsigned apply_hit_c, apply_fail_c;
};

}

#endif

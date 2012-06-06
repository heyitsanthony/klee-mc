#ifndef BENCHMARKER_H
#define BENCHMARKER_H

#include "klee/ExprBuilder.h"

namespace klee
{
class Solver;
class Benchmarker
{
public:
class DualBuilder
{
public:
	virtual ~DualBuilder() {}
	virtual void getDuals(ref<Expr>& gen_1, ref<Expr>& gen_2) = 0;
protected:
	DualBuilder(ref<Array>& _arr)
	: arr(_arr) {}

	ref<Array>	arr;
private:

};

	Benchmarker(Solver* _s, ExprBuilder::BuilderKind _bk)
	: s(_s), bk(_bk) {}
	virtual ~Benchmarker(void) {}

	/* averages completion time of query (e == 0) */
	double benchExpr(ref<Expr>& e);

	/* returns the relative error */
	double benchRule(const ExprRule* er);

	void benchRules(void);

	/* benchmark rule builder rules against arbitrary builder */
	void benchRuleBuilder(ExprBuilder *eb);

private:
	bool genTestExprs(
		Benchmarker::DualBuilder* db,
		ref<Expr>& gen_from,
		ref<Expr>& gen_to);

	void genBuilderExprs(
		ref<Array>& arr,
		const ref<Expr>& base_expr,
		ExprBuilder	*eb1,
		ExprBuilder	*eb2,
		ref<Expr>& gen_expr_eb1,
		ref<Expr>& gen_expr_eb2);

	double benchDuals(DualBuilder* db);
	DualBuilder* getRuleDual(const ExprRule* er);

	Solver				*s;
	ExprBuilder::BuilderKind	bk;
};
}

#endif

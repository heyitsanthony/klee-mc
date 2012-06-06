#include <llvm/Support/CommandLine.h>
#include <iostream>
#include "klee/Internal/Support/Timer.h"
#include "klee/Internal/ADT/RNG.h"
#include "../../lib/Expr/ExprRule.h"
#include "../../lib/Expr/RuleBuilder.h"
#include "klee/Expr.h"
#include "klee/ExprBuilder.h"
#include "klee/Solver.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/Assignment.h"
#include "static/Sugar.h"

#include "ExprGen.h"
#include "Benchmarker.h"

#define NUM_BENCH_ITER	(5+2)

using namespace klee;
using namespace llvm;

namespace {
	cl::opt<bool>
	ParanoidBenchmark(
		"paranoid-benchmark",
		cl::desc("Check all queries in benchmark for equivalence."),
		cl::init(false));
}

double Benchmarker::benchExpr(ref<Expr>& e)
{
	Query		q(
			EqExpr::create(
				ConstantExpr::create(0, e->getWidth()),
				e));
	double		dat[NUM_BENCH_ITER];
	double		total_time, avg_time;
	unsigned	min_time_idx, max_time_idx;

	for (unsigned i = 0; i < NUM_BENCH_ITER; i++) {
		WallTimer	wt;
		bool		ok, maybeTrue;

		ok = s->mayBeTrue(q, maybeTrue);
		if (ok == false)
			return 1.0/0.0; // infty

		dat[i] = wt.checkSecs();
	}

	/* throw out outliers */
	max_time_idx = min_time_idx = 0;
	for (unsigned i = 0; i < NUM_BENCH_ITER; i++) {
		if (dat[i] < dat[min_time_idx])
			min_time_idx = i;
		if (dat[i] > dat[max_time_idx])
			max_time_idx = i;
	}

	dat[min_time_idx] = 0;
	dat[max_time_idx] = 0;

	for (unsigned i = 0; i < NUM_BENCH_ITER; i++)
		total_time += dat[i];

	avg_time = total_time / ((double)NUM_BENCH_ITER-2);
	return avg_time;
}

class DualEB : public Benchmarker::DualBuilder
{
public:
	DualEB(	ref<Array>& _arr,
		ref<Expr>& _base_expr,
		ExprBuilder* eb1, ExprBuilder* eb2)
	: Benchmarker::DualBuilder(_arr)
	, base_expr(_base_expr)
	{
		eb[0] = eb1;
		eb[1] = eb2;
	}

	virtual ~DualEB(void) {}
	virtual void getDuals(ref<Expr>& gen_1, ref<Expr>& gen_2);
private:
	ref<Expr> 	base_expr;
	ExprBuilder	*eb[2];
};

void DualEB::getDuals(ref<Expr>& gen_expr_eb1, ref<Expr>& gen_expr_eb2)
{
	ExprBuilder	*init_eb;

	init_eb = Expr::getBuilder();

	do {
		LoggingRNG	rng;
		ReplayRNG	*replay;

		Expr::setBuilder(eb[0]);
		gen_expr_eb1 = ExprGen::genExpr(rng, base_expr, arr, 10);
		replay = rng.getReplay();

		Expr::setBuilder(eb[1]);
		gen_expr_eb2 = ExprGen::genExpr(*replay, base_expr, arr, 10);
		delete replay;
	} while (
		gen_expr_eb1->getKind() == Expr::Constant &&
		gen_expr_eb2->getKind() == Expr::Constant);

	Expr::setBuilder(init_eb);
}

class DualExprs : public Benchmarker::DualBuilder
{
public:
	DualExprs(ref<Array>& _arr,
		ref<Expr>& _base_from,
		ref<Expr>& _base_to)
	: Benchmarker::DualBuilder(_arr)
	, base_from(_base_from)
	, base_to(_base_to)
	{}

	virtual ~DualExprs() {}
	virtual void getDuals(ref<Expr>& gen_1, ref<Expr>& gen_2);
private:
	ref<Expr> base_from, base_to;
};

void DualExprs::getDuals(ref<Expr>& gen_from, ref<Expr>& gen_to)
{
	do {
		LoggingRNG	rng;
		ReplayRNG	*replay;

		gen_from = ExprGen::genExpr(rng, base_from, arr, 10);
		replay = rng.getReplay();
		gen_to = ExprGen::genExpr(*replay, base_to, arr, 10);
		delete replay;
	} while (
		gen_from->getKind() == Expr::Constant &&
		gen_to->getKind() == Expr::Constant);
}

bool Benchmarker::genTestExprs(
	Benchmarker::DualBuilder* db,
	ref<Expr>& gen_from,
	ref<Expr>& gen_to)
{
	bool		mustBeTrue;

	db->getDuals(gen_from, gen_to);

	if (ParanoidBenchmark && s->mustBeTrue(
		Query(EqExpr::create(gen_from, gen_to)),
		mustBeTrue) && !mustBeTrue)
	{
		std::cerr << "[BENCHMARK] !!! SAME OPS. NOT EQUAL !!!?!\n";
		std::cerr << "GEN-FROM: " << gen_from << '\n';
		std::cerr << "GEN-TO: " << gen_to << '\n';

		return false;
	}

	return true;
}

double Benchmarker::benchDuals(DualBuilder* db)
{
	ref<Expr>	gen_from, gen_to;
	double		from_time, to_time, rel_err;

	if (!genTestExprs(db, gen_from, gen_to))
		return 1.0/0.0;

	from_time = benchExpr(gen_from);
	to_time = benchExpr(gen_to);
	rel_err = (to_time - from_time)/from_time;
	if (rel_err > 0.05) {
		std::cerr << "GEN-FROM: " << gen_from << '\n';
		std::cerr << "GEN-TO: " << gen_to << '\n';
		std::cerr	<< "FROM-TIME=" << from_time
				<< ". TO-TIME=" << to_time
				<< ". RELERR=" << rel_err << '\n';
	}

	return rel_err;
}

Benchmarker::DualBuilder* Benchmarker::getRuleDual(const ExprRule* er)
{
	ref<Expr>	base_from, base_to;
	ref<Array>	arr;
	bool		mustBeTrue;

	base_to = er->getToExpr();
	/* no reason to measure constant rules-- if solving is slower
	 * with a constant, then the solver is broken */
	if (base_to->getKind() == Expr::Constant)
		return NULL; // -infty

	base_from = er->getFromExpr();
	if (ParanoidBenchmark && s->mustBeTrue(
		Query(EqExpr::create(base_from, base_to)),
		mustBeTrue) && !mustBeTrue)
	{
		std::cerr << "[BENCHMARK] !!! BASES NOT EQUAL !!!?!\n";
		return NULL;
	}

	arr = er->getMaterializeArray();
	return new DualExprs(arr, base_from, base_to);
}

double Benchmarker::benchRule(const ExprRule* er)
{
	DualBuilder	*de;
	double		rel_err;

	de = getRuleDual(er);
	if (de == NULL) {
		if (er->getToExpr()->getKind() == Expr::Constant)
			return -1.0/0.0;
		return 1.0/0.0;
	}

	rel_err = benchDuals(de);
	delete de;

	return rel_err;
}

void Benchmarker::benchRules(void)
{
	RuleBuilder	*rb;
	unsigned	i = 0;

	rb = new RuleBuilder(ExprBuilder::create(bk));
	foreach (it, rb->begin(), rb->end()) {
		double rel_err;

		std::cerr << "Benchmarking rule #" << ++i << '\n';
		rel_err = benchRule(*it);
		if (rel_err > 0.05) {
			std::cerr << "Retrying benchmark #" << i << "\n";
			benchRule(*it);
		}
	}

	delete rb;
}

void Benchmarker::benchRuleBuilder(ExprBuilder *eb)
{
	RuleBuilder	*rb;
	unsigned	i = 0;

	rb = new RuleBuilder(ExprBuilder::create(bk));
	foreach (it, rb->begin(), rb->end()) {
		const ExprRule	*er(*it);
		ref<Array>	arr(er->getMaterializeArray());
		ref<Expr>	from_e(er->getFromExpr());
		DualEB		deb(arr, from_e, rb, eb);
		double		rel_err;

		std::cerr << "Benchmarking rule #" << ++i << '\n';

		rel_err = benchDuals(&deb);
		if (rel_err > 0.05) {
			std::cerr << "Retrying benchmark #" << i << "\n";
			benchDuals(&deb);
		}
	}

	delete rb;
}
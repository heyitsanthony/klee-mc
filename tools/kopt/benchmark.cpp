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

#define NUM_BENCH_ITER	(5+2)

using namespace klee;

extern ExprBuilder::BuilderKind	BuilderKind;

static double benchmarkExpr(ref<Expr>& e, Solver* s)
{
	Query		q(
			EqExpr::create(
				ConstantExpr::create(0, e->getWidth()),
				e));
	double		dat[NUM_BENCH_ITER];
	double		total_time, avg_time;

	for (unsigned i = 0; i < NUM_BENCH_ITER; i++) {
		WallTimer	wt;
		bool		ok, maybeTrue;

		ok = s->mayBeTrue(q, maybeTrue);
		if (ok == false)
			return 1.0/0.0; // infty

		dat[i] = wt.checkSecs();
	}

	/* throw out outliers */
	unsigned	min_time_idx, max_time_idx;
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

static double benchmarkRule(ExprRule* er, Solver *s)
{
	ref<Expr>	base_from, base_to;
	ref<Expr>	gen_from, gen_to;
	ref<Array>	arr;

	base_to = er->getToExpr();
	/* no reason to measure constant rules-- if solving is slower
	 * with a constant, then the solver is broken */
	if (base_to->getKind() == Expr::Constant)
		return -1.0/0.0; // -infty

	base_from = er->getFromExpr();
	arr = er->getMaterializeArray();

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

	double from_time, to_time, rel_err;

	from_time = benchmarkExpr(gen_from, s);
	to_time = benchmarkExpr(gen_to, s);
	rel_err = (to_time - from_time)/from_time;
	if (rel_err > 0.05) {
		std::cerr << "BASE-FROM: " << base_from << '\n';
		std::cerr << "BASE-TO: " << base_to << '\n';

		std::cerr	<< "FROM-TIME=" << from_time
				<< ". TO-TIME=" << to_time
				<< ". RELERR=" << rel_err << '\n';

	}

	return rel_err;
}

void benchmarkRules(ExprBuilder *eb, Solver* s)
{
	RuleBuilder	*rb;
	unsigned	i = 0;

	rb = new RuleBuilder(ExprBuilder::create(BuilderKind));
	foreach (it, rb->begin(), rb->end()) {
		double rel_err;

		std::cerr << "Benchmarking rule #" << ++i << '\n';
		rel_err = benchmarkRule(*it, s);
		if (rel_err > 0.05) {
			std::cerr << "Retrying benchmark #" << i << "\n";
			benchmarkRule(*it, s);
		}
	}

	delete rb;
}

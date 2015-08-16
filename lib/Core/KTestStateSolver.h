#ifndef KLEE_KTESTSTATESOLVER_H
#define KLEE_KTESTSTATESOLVER_H

#include "StateSolver.h"

struct KTest;

namespace klee
{
class Executor;
class ExecutionState;
class Solver;

class KTestStateSolver : public StateSolver
{
public:
	KTestStateSolver(
		StateSolver* _base,
		ExecutionState& base_es,
		const KTest* _kt);

	virtual ~KTestStateSolver();

	bool mustBeTrue(const ExecutionState&, ref<Expr>, bool &res) override;
	bool mustBeFalse(const ExecutionState&, ref<Expr>, bool &res) override;

	bool getRange(
		const ExecutionState& es,
		ref<Expr> query,
		std::pair< ref<Expr>, ref<Expr> >& ret) override
	{ return base->getRange(es, query, ret); }

	Solver *getSolver(void) override { return base->getSolver(); }

	bool evaluate(	const ExecutionState&,
			ref<Expr>,
			Solver::Validity &res) override;

	bool mayBeTrue(const ExecutionState&, ref<Expr>, bool &res) override;
	bool mayBeFalse(const ExecutionState&, ref<Expr>, bool &res) override;
	bool getValue(
		const ExecutionState &,
		ref<Expr> expr,
		ref<ConstantExpr> &result,
		ref<Expr> pred = 0) override;
	bool getInitialValues(const ExecutionState&, Assignment&) override;
	ref<Expr> toUnique(const ExecutionState &state,
			   const ref<Expr> &e) override;
private:
	bool updateArrays(const ExecutionState& state);

	StateSolver	*base;

	const KTest	*kt;
	Assignment	*kt_assignment;
	std::vector<ref<Array> > arrs;
	unsigned	base_objs;
};
}
#endif

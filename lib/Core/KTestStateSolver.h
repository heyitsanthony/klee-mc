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

	bool mustBeTrue(const ExecutionState& es, ref<Expr> e, bool &result);

	bool mustBeFalse(const ExecutionState& es, ref<Expr> e, bool &result);

	bool getRange(
		const ExecutionState& es,
		ref<Expr> query,
		std::pair< ref<Expr>, ref<Expr> >& ret)
	{ return base->getRange(es, query, ret); }

	bool evaluate(const ExecutionState&, ref<Expr>, Solver::Validity &result);
	bool mayBeTrue(const ExecutionState&, ref<Expr>, bool &result);
	bool mayBeFalse(const ExecutionState&, ref<Expr>, bool &result);
	bool getValue(
		const ExecutionState &,
		ref<Expr> expr,
		ref<ConstantExpr> &result,
		ref<Expr> pred = 0);
	bool getInitialValues(const ExecutionState&, Assignment&);
	ref<Expr> toUnique(const ExecutionState &state, const ref<Expr> &e);
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

#ifndef KLEE_COSTKILLERSTATESOLVER_H
#define KLEE_COSTKILLERSTATESOLVER_H

#include "klee/ExecutionState.h"
#include "StateSolver.h"

#define CKSS_CHK(es) if (es.queryCost > maxTime) return false;

namespace klee
{
class Executor;
class ExecutionState;
class Solver;

class CostKillerStateSolver : public StateSolver
{
public:
	CostKillerStateSolver(StateSolver* _base, double _maxTime)
	: StateSolver(NULL, NULL)
	, base(_base), maxTime(_maxTime) { solver = NULL; }
	virtual ~CostKillerStateSolver() {}

	bool mustBeTrue(const ExecutionState& es, ref<Expr> e, bool &result)
	{	CKSS_CHK(es);
		return base->mustBeTrue(es, e, result); }

	bool mustBeFalse(const ExecutionState& es, ref<Expr> e, bool &result)
	{	CKSS_CHK(es);
		return base->mustBeFalse(es, e, result); }

	bool getRange(
		const ExecutionState& es,
		ref<Expr> query,
		std::pair< ref<Expr>, ref<Expr> >& ret)
	{	CKSS_CHK(es);
		return base->getRange(es, query, ret); }

	bool evaluate(
		const ExecutionState& es,
		ref<Expr> e, Solver::Validity &result)
	{	CKSS_CHK(es);
		return base->evaluate(es, e, result); }

	bool mayBeTrue(const ExecutionState& es, ref<Expr> e, bool &result)
	{ CKSS_CHK(es); return base->mayBeTrue(es, e, result); }

	bool mayBeFalse(const ExecutionState& es, ref<Expr> e, bool &result)
	{ CKSS_CHK(es); return base->mayBeFalse(es, e, result); }
	bool getValue(
		const ExecutionState &es,
		ref<Expr> expr,
		ref<ConstantExpr> &result,
		ref<Expr> pred = 0)
	{ CKSS_CHK(es); return base->getValue(es, expr, result, pred); }
	bool getInitialValues(const ExecutionState& es, Assignment& a)
	{ CKSS_CHK(es); return base->getInitialValues(es, a); }

	ref<Expr> toUnique(const ExecutionState &es, const ref<Expr> &e)
	{ if (es.queryCost > maxTime) return NULL;
	  return base->toUnique(es, e);}
private:
	StateSolver	*base;
	double		maxTime; /* in seconds */
};
}

#undef CKSS_CHK
#endif

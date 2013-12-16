#include "Executor.h"
#include "Forks.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/util/Assignment.h"
#include "KTestStateSolver.h"

using namespace klee;

KTestStateSolver::KTestStateSolver(
	StateSolver* _base,
	ExecutionState& es_base,
	const KTest* _kt)
: StateSolver(_base->solver, _base->timedSolver, _base->simplifyExprs)
, base(_base)
, kt(_kt)
{
	solver = NULL;
	
	kt_assignment = new Assignment(true);

	/* augment with base state's arrays */
	foreach (it, es_base.symbolicsBegin(), es_base.symbolicsEnd())
		arrs.push_back(it->getArrayRef());

	base_objs = arrs.size();
	assert (es_base.getNumSymbolics() == base_objs);
}


KTestStateSolver::~KTestStateSolver()
{
	delete kt_assignment;
}

bool KTestStateSolver::evaluate(
	const ExecutionState& es,
	ref<Expr> e,
	Solver::Validity &result)
{
	if (updateArrays(es) == false) goto done;
	e = kt_assignment->evaluate(e);
done:	return base->evaluate(es, e, result);
}

bool KTestStateSolver::mayBeTrue(
	const ExecutionState& es, ref<Expr> e, bool &result)
{
	ref<Expr>	expr;
	if (updateArrays(es) == false) goto done;
	expr = kt_assignment->evaluate(e);
	if (expr->isTrue()) {
		result = true;
		return true;
	}
done:	return base->mayBeTrue(es, e, result);
}

bool KTestStateSolver::mayBeFalse(
	const ExecutionState& es, ref<Expr> e, bool &result)
{
	ref<Expr>	expr;
	if (updateArrays(es) == false) goto done;
	expr = kt_assignment->evaluate(e);
	if (expr->isFalse()) {
		result = true;
		return true;
	}
done:	return base->mayBeFalse(es, e, result);
}

bool KTestStateSolver::mustBeTrue(
	const ExecutionState& es, ref<Expr> e, bool &result)
{
	ref<Expr>	expr;
	if (updateArrays(es) == false) goto done;
	expr = kt_assignment->evaluate(e);
	if (expr->isFalse()) {
		result = false;
		return true;
	}
done:	return base->mustBeTrue(es, e, result);
}

bool KTestStateSolver::mustBeFalse(
	const ExecutionState& es, ref<Expr> e, bool &result)
{
	ref<Expr>	expr;
	if (updateArrays(es) == false) goto done;
	expr = kt_assignment->evaluate(e);
	if (expr->isTrue()) {
		result = false;
		return true;
	}
done:	return base->mustBeFalse(es, e, result);
}

bool KTestStateSolver::getValue(
	const ExecutionState &es,
	ref<Expr> e,
	ref<ConstantExpr> &result,
	ref<Expr> pred)
{
	if (updateArrays(es) == false) goto done;
	e = kt_assignment->evaluate(e);
done:	return base->getValue(es, e, result, pred);
}

bool KTestStateSolver::getInitialValues(
	const ExecutionState& es, Assignment& a)
{
// XXX
//	if (updateArrays(es) == false) goto done;
//	e = kt_assignment->evaluate(e);
	return base->getInitialValues(es, a);
}

ref<Expr> KTestStateSolver::toUnique(const ExecutionState &es, const ref<Expr> &e)
{
	if (updateArrays(es) == false) goto done;
	e = kt_assignment->evaluate(e);
done:	return base->toUnique(es, e);
}

bool KTestStateSolver::updateArrays(const ExecutionState& es)
{
	ExecutionState::SymIt		it(es.symbolicsBegin());
	unsigned			i;

	if (arrs.size() == es.getNumSymbolics())
		return true;

	assert (arrs.size() < es.getNumSymbolics());

	for (i = 0; i < arrs.size(); i++) it++;

	for (; i < es.getNumSymbolics(); i++) {
		const SymbolicArray	&sa(*it);
		unsigned		oi = i - base_objs;
		ref<Array>		arr(sa.getArrayRef());
		const uint8_t		*v_buf = kt->objects[oi].bytes;
		unsigned		v_len = kt->objects[oi].numBytes;

		if (arr->getSize() != kt->objects[oi].numBytes)
			return false;

		arrs.push_back(arr);

		std::vector<uint8_t>	v(v_buf, v_buf + v_len);
		kt_assignment->addBinding(arr.get(), v);
		it++;
	}

	return true;
}


#include "klee/util/Assignment.h"
#include "IncompleteSolver.h"
#include "StagedSolver.h"

using namespace klee;

StagedIncompleteSolverImpl::StagedIncompleteSolverImpl(
	IncompleteSolver *_primary, Solver *_secondary)
: primary(_primary)
, secondary(_secondary)
{}

StagedIncompleteSolverImpl::~StagedIncompleteSolverImpl()
{
	delete primary;
	delete secondary;
}

bool StagedIncompleteSolverImpl::computeSat(const Query& query)
{
	IncompleteSolver::PartialValidity trueResult;
	bool	isSat;

	trueResult = primary->computeTruth(query);
	if (	trueResult == IncompleteSolver::MustBeTrue ||
		trueResult == IncompleteSolver::MayBeTrue ||
		trueResult == IncompleteSolver::TrueOrFalse)
	{
		return true;
	}

	isSat = secondary->impl->computeSat(query);
	if (secondary->impl->failed()) failQuery();

	return isSat;
}

Solver::Validity StagedIncompleteSolverImpl::computeValidity(const Query& query)
{
	Solver::Validity	v;
	bool			isSat;

	switch (primary->computeValidity(query)) {
	case IncompleteSolver::MustBeTrue:
		return Solver::True;
	case IncompleteSolver::MustBeFalse:
		return Solver::False;
	case IncompleteSolver::TrueOrFalse:
		return Solver::Unknown;
	case IncompleteSolver::MayBeTrue:
		isSat = secondary->impl->computeSat(query.negateExpr());
		if (secondary->impl->failed())
			break;
		return isSat ? Solver::Unknown : Solver::True;
	case IncompleteSolver::MayBeFalse:
		isSat = secondary->impl->computeSat(query);
		if (secondary->impl->failed())
			break;
		/* negation not sat => must be valid */
		return isSat ? Solver::Unknown : Solver::False;
	default:
		v = secondary->impl->computeValidity(query);
		if (secondary->impl->failed())
			break;
		return v;
	}

	failQuery();
	return Solver::Unknown;
}

ref<Expr> StagedIncompleteSolverImpl::computeValue(const Query& query)
{
	ref<Expr> result;

	result = primary->computeValue(query);
	if (!primary->failed()) return result;

	result = secondary->impl->computeValue(query);
	if (secondary->impl->failed()) failQuery();

	return result;
}

bool StagedIncompleteSolverImpl::computeInitialValues(
	const Query& query,
	Assignment& a)
{
	bool hasSol;

	hasSol = primary->computeInitialValues(query, a);
	if (!primary->failed()) return hasSol;

	a.resetBindings();
	hasSol = secondary->impl->computeInitialValues(query, a);
	if (secondary->impl->failed()) failQuery();

	return hasSol;
}

StagedSolverImpl::StagedSolverImpl(
	Solver *_primary, Solver *_secondary, bool _vs)
: primary(_primary)
, secondary(_secondary)
, validityBySat(_vs)
{}

StagedSolverImpl::~StagedSolverImpl()
{
	delete primary;
	delete secondary;
}

bool StagedSolverImpl::computeSat(const Query& query)
{
	bool	isSat;

	isSat = primary->impl->computeSat(query);
	if (!primary->impl->failed()) return isSat;
	primary->impl->ackFail();

	isSat = secondary->impl->computeSat(query);
	if (secondary->impl->failed()) failQuery();
	
	return isSat;
}

Solver::Validity StagedSolverImpl::computeValidity(const Query& query)
{
	Solver::Validity	v;

	if (validityBySat)
		return SolverImpl::computeValidity(query);

	v = primary->impl->computeValidity(query);
	if (!primary->impl->failed()) return v;
	primary->impl->ackFail();

	v = secondary->impl->computeValidity(query);
	if (secondary->impl->failed()) failQuery();

	return v;
}

ref<Expr> StagedSolverImpl::computeValue(const Query& query)
{
	ref<Expr> result;

	result = primary->impl->computeValue(query);
	if (!primary->impl->failed())
		return result;
	primary->impl->ackFail();

	result = secondary->impl->computeValue(query);
	if (secondary->impl->failed()) failQuery();

	return result;
}

bool StagedSolverImpl::computeInitialValues(
	const Query& query,
	Assignment& a)
{
	bool hasSol;

	hasSol = primary->impl->computeInitialValues(query, a);
	if (!primary->failed() && hasSol)
		return hasSol;
	primary->impl->ackFail();

	hasSol = secondary->impl->computeInitialValues(query, a);
	if (secondary->impl->failed()) failQuery();
	return hasSol;
}

void StagedSolverImpl::failQuery(void)
{
	secondary->impl->ackFail();
	SolverImpl::failQuery();
}

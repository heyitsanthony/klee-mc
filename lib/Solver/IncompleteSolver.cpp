//===-- IncompleteSolver.cpp ----------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "IncompleteSolver.h"

#include "klee/Constraints.h"

using namespace klee;

/***/

IncompleteSolver::PartialValidity
IncompleteSolver::negatePartialValidity(PartialValidity pv) {
  switch(pv) {
  default: assert(0 && "invalid partial validity");
  case MustBeTrue:  return MustBeFalse;
  case MustBeFalse: return MustBeTrue;
  case MayBeTrue:   return MayBeFalse;
  case MayBeFalse:  return MayBeTrue;
  case TrueOrFalse: return TrueOrFalse;
  }
}

IncompleteSolver::PartialValidity
IncompleteSolver::computeValidity(const Query& query)
{
	PartialValidity trueResult;

	trueResult = computeTruth(query);
	if (trueResult == MustBeTrue) return MustBeTrue;

	PartialValidity falseResult;
	falseResult = computeTruth(query.negateExpr());
	if (falseResult == MustBeTrue) return MustBeFalse;

	bool trueCorrect, falseCorrect;

	trueCorrect = trueResult != None;
	falseCorrect = falseResult != None;

	if (trueCorrect && falseCorrect) return TrueOrFalse;
	if (trueCorrect) return MayBeFalse;
	if (falseCorrect) return MayBeTrue;

	return None;
}

/***/

StagedSolverImpl::StagedSolverImpl(IncompleteSolver *_primary, Solver *_secondary)
: primary(_primary)
, secondary(_secondary) { }

StagedSolverImpl::~StagedSolverImpl()
{
  delete primary;
  delete secondary;
}

bool StagedSolverImpl::computeSat(const Query& query)
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

Solver::Validity StagedSolverImpl::computeValidity(const Query& query)
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

ref<Expr> StagedSolverImpl::computeValue(const Query& query)
{
	ref<Expr> result;
	result = primary->computeValue(query);
	if (!primary->failed()) return result;

	result = secondary->impl->computeValue(query);
	if (secondary->impl->failed()) failQuery();

	return result;
}

bool StagedSolverImpl::computeInitialValues(
	const Query& query,
	const std::vector<const Array*> &objects,
	std::vector< std::vector<unsigned char> > &values)
{
  bool hasSolution;

  hasSolution = primary->computeInitialValues(query, objects, values);
  if (!primary->failed()) return hasSolution;

  hasSolution = secondary->impl->computeInitialValues(query, objects, values);
  if (secondary->impl->failed()) failQuery();

  return hasSolution;
}

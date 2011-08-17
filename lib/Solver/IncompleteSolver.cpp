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

IncompleteSolver::PartialValidity
IncompleteSolver::negatePartialValidity(PartialValidity pv)
{
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

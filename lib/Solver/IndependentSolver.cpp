//===-- IndependentSolver.cpp ---------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include <llvm/Support/CommandLine.h>

#include "klee/Expr.h"
#include "klee/Constraints.h"
#include "klee/IndependentElementSet.h"
#include "static/Sugar.h"
#include "IndependentSolver.h"

#include <map>
#include <vector>
#include <iostream>

using namespace klee;
using namespace llvm;

uint64_t IndependentSolver::indep_c = 0;

namespace {
/* this is optional because it can break path replay */
  cl::opt<bool> RandomizeComputeValue(
  	"randomize-independent-solver",
	cl::desc("Randomize unconstrained values in independent solver"),
	cl::init(false));
}

#if 0
    std::set< ref<Expr> > reqset(result.begin(), result.end());
    std::cerr << "--\n";
    std::cerr << "Q: " << query.expr << "\n";
    std::cerr << "\telts: " << IndependentElementSet(query.expr) << "\n";
    int i = 0;
  foreach (it, query.constraints.begin(), query.constraints.end()) {
      std::cerr << "C" << i++ << ": " << *it;
      std::cerr << " " << (reqset.count(*it) ? "(required)" : "(independent)") << "\n";
      std::cerr << "\telts: " << IndependentElementSet(*it) << "\n";
    }
    std::cerr << "elts closure: " << eltsClosure << "\n";
#endif

#define SETUP_CONSTRAINTS			\
	std::vector< ref<Expr> > required;	\
	IndependentElementSet eltsClosure;	\
	eltsClosure = IndependentElementSet::getIndependentConstraints(	\
		query, required);	\
	ConstraintManager tmp(required);	\
	if (isUnconstrained(tmp, query))

static bool isFreeRead(const ref<Expr>& e)
{
	const ReadExpr		*re;
	const ConstantExpr	*ce;

	re = dyn_cast<ReadExpr>(e);
	if (re != NULL)
		return (re->updates.getSize() == 0 &&
			re->index->getKind() == Expr::Constant);

	std::set<unsigned>	indices;

	while (e->getKind() == Expr::Concat) {
		ref<Expr>	lhs(e->getKid(0));
		uint64_t		idx;

		re = dyn_cast<ReadExpr>(lhs);
		if (re == NULL || re->updates.getSize())
			return false;

		ce = dyn_cast<ConstantExpr>(re->index);
		if (ce == NULL)
			return false;

		idx = ce->getZExtValue();
		if (indices.count(idx))
			return false;

		indices.insert(idx);
		e = e->getKid(1);
	}

	re = dyn_cast<ReadExpr>(e);
	if (re == NULL)
		return false;

	ce = dyn_cast<ConstantExpr>(re->index);
	if (ce == NULL)
		return false;

	if (indices.count(ce->getZExtValue()))
		return false;

	return true;
}

static bool isFreeExpr(const ref<Expr> e)
{
	bool	is_lhs_free, is_rhs_free;

	if (	e->getKind() == Expr::Eq &&
		e->getKid(0)->isZero() &&
		e->getKid(1)->getKind() == Expr::Eq)
	{
		return isFreeExpr(e->getKid(1));
	}

	if (	e->getKind() == Expr::Eq ||
		e->getKind() == Expr::Ule ||
		e->getKind() == Expr::Ult)
	{
		is_lhs_free = isFreeRead(e->getKid(0));
		is_rhs_free = isFreeRead(e->getKid(1));
	}

	if (	e->getKind() == Expr::Eq && is_rhs_free &&
		e->getKid(0)->getKind() == Expr::Constant)
	{
		/* this was breaking on (Eq (x) (extract [7:0] x))
		 * before the I added a constant check */
		return true;
	}

	if (e->getKind() == Expr::Extract)
		return isFreeRead(e->getKid(0));

	if (	(e->getKind() == Expr::Ule || e->getKind() == Expr::Ult) &&
		(is_lhs_free || is_rhs_free))
	{
		ConstantExpr	*ce;

		/* lhs is const? */
		ce = dyn_cast<ConstantExpr>(e->getKid(0));
		if (ce != NULL && !ce->isZero())
			return true;

		/* rhs is const? */
		ce = dyn_cast<ConstantExpr>(e->getKid(1));
		if (ce != NULL && !ce->isZero())
			return true;

	}

	return false;
}

static bool isUnconstrained(
	const ConstraintManager& cm, const Query& query)
{
	if (cm.size() != 0)
		return false;

	if (isFreeExpr(query.expr))
		return true;

//	std::cerr << "MISSED UNCONSTRAINED: " << query.expr << '\n';
	return false;
}

Solver::Validity IndependentSolver::computeValidity(const Query& query)
{
	SETUP_CONSTRAINTS { indep_c++; return Solver::Unknown; }
	return doComputeValidity(Query(tmp, query.expr));
}

bool IndependentSolver::computeSat(const Query& query)
{
	SETUP_CONSTRAINTS { indep_c++; return true; }
	return doComputeSat(Query(tmp, query.expr));
}

ref<Expr> IndependentSolver::computeValue(const Query& query)
{
	SETUP_CONSTRAINTS {
		uint64_t	v;
		if (RandomizeComputeValue) {
			v = rng.getInt32();
			v <<= 32;
			v |= rng.getInt32();
			if (query.expr->getWidth() < 64)
				v &= (1 << query.expr->getWidth()) - 1;
		} else
			v = query.hash();

		indep_c++;
		return MK_CONST(v, query.expr->getWidth());
	}

	return doComputeValue(Query(tmp, query.expr));
}

Solver *klee::createIndependentSolver(Solver *s)
{ return new Solver(new IndependentSolver(s)); }

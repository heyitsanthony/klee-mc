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

#define SETUP_CONSTRAINTS_			\
	ConstraintManager	cs;		\
	IndependentElementSet eltsClosure;	\
	eltsClosure = IndependentElementSet::getIndependentConstraints(	\
		query, cs);			\
	ConstraintManager cs2;

//#define PARANOIA
#ifdef PARANOIA
#define	SETUP_PARANOIA						\
	Query q2(cs2, Expr::createImplies(			\
			cs.getConjunction(),			\
			query.constraints.getConjunction()));	\
	Query q3(cs2, Expr::createImplies(			\
			query.constraints.getConjunction(),	\
			cs.getConjunction()));			\
	if (cs.size() != query.constraints.size()) 		\
		std::cerr << cs.size() << "<"			\
			<< query.constraints.size() << '\n';	\
	assert (doComputeValidity(q2) != Solver::False);	\
	assert (doComputeValidity(q3) == Solver::True);
#else
#define SETUP_PARANOIA	;
#endif

#define SETUP_CONSTRAINTS			\
	SETUP_CONSTRAINTS_			\
	SETUP_PARANOIA				\
	if (isUnconstrained(cs, query))

#define SETUP_CONSTRAINTS_STATIC		\
	SETUP_CONSTRAINTS_			\
	if (isUnconstrained(cs, query))

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
	bool	is_lhs_free = false, is_rhs_free = false;

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

	return false;
}

Query IndependentSolver::getIndependentQuery(
	const Query& query, ConstraintManager& cs_)
{
	SETUP_CONSTRAINTS_STATIC { return Query(query.expr); }
	cs_ = cs;
	return Query(cs_, query.expr);
}

Solver::Validity IndependentSolver::computeValidity(const Query& query)
{
	SETUP_CONSTRAINTS { indep_c++; return Solver::Unknown; }
	return doComputeValidity(Query(cs, query.expr));
}

bool IndependentSolver::computeSat(const Query& query)
{
	SETUP_CONSTRAINTS { indep_c++; return true; }
	return doComputeSat(Query(cs, query.expr));
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

	return doComputeValue(Query(cs, query.expr));
}

Solver *klee::createIndependentSolver(Solver *s)
{ return new Solver(new IndependentSolver(s)); }

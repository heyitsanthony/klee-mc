#include <iostream>
#include "klee/Constraints.h"
#include "klee/Solver.h"

using namespace klee;

bool Solver::getUsefulBits(const Query& q, uint64_t& bits)
{
	ref<Expr>	e(q.expr);
	Expr::Width	width = e->getWidth();
	uint64_t	lo, mid, hi;

	bits=0;
	lo=0;
	hi=width;
	while (lo < hi) {
		bool mbt, ok;

		mid = lo + (hi - lo)/2;

		ok = mustBeTrue(q.withExpr(
			MK_EQ(	MK_LSHR(e, MK_CONST(mid, width)),
				MK_CONST(0, width))),
			mbt);

		if (ok == false) return false;

		if (mbt) {
			hi = mid;
		} else {
			lo = mid+1;
		}

		bits = lo;
	}

	return true;
}

// binary search for max
bool Solver::getRangeMax(
	const Query& q, uint64_t bits, uint64_t min, uint64_t& max)
{
	ref<Expr>	e(q.expr);
	Expr::Width	w = e->getWidth();
	uint64_t	lo, hi;

	lo=min;
	hi=bits64::maxValueOfNBits(bits);
	while (lo < hi) {
		uint64_t	mid;
		bool		mbt;
		ref<Expr>	cmp_e;

		mid = lo + (hi - lo)/2;

		cmp_e = MK_ULE(e, MK_CONST(mid, w));
		if (mustBeTrue(q.withExpr(cmp_e), mbt) == false)
			return false;

		if (mbt) {
			hi = mid;
		} else {
			lo = mid+1;
		}
	}

	max = lo;
	return true;
}

bool Solver::getRangeMin(const Query& q, uint64_t bits, uint64_t& min)
{
	ref<Expr>		e(q.expr);
	Expr::Width		w = e->getWidth();
	uint64_t		lo, hi;
	bool			ok, mbt;

	// check common case: min == 0
	ok = mayBeTrue(q.withExpr(MK_EQ(e, MK_CONST(0, w))), mbt);
	if (ok == false)
		return false;

	if (mbt) {
		min = 0;
		return true;
	}

	// binary search for min
	lo=0;
	hi=bits64::maxValueOfNBits(bits);
	while (lo<hi) {
		uint64_t	mid;

		mid = lo + (hi - lo)/2;
		ok = mustBeTrue(q.withExpr(MK_ULE(e, MK_CONST(mid, w))), mbt);
		if (ok == false)
			return false;

		if (mbt) {
			hi = mid;
		} else {
			lo = mid+1;
		}
	}

	min = lo;
	return true;
}

bool Solver::fastGetRange(
	const Query& query,
	std::pair< ref<Expr>, ref<Expr> >& ret,
	bool	&ok)
{
	ref<Expr>		e(query.expr);
	Solver::Validity	result;
	Expr::Width		w = e->getWidth();
	uint64_t		min, max;

	if (dyn_cast<ConstantExpr>(e) != NULL) {
		ret = std::make_pair(e, e);
		ok = true;
		return true;
	}

	/* not much range checking necessary for a single bit */
	if (w != 1)
		return false;


	if (!evaluate(query, result)) {
		ok = false;
		return true;
	}

	switch (result) {
	case Solver::True:	min = max = 1; break;
	case Solver::False:	min = max = 0; break;
	default:		min = 0, max = 1; break;
	}

	ret = std::make_pair(MK_CONST(min, w), MK_CONST(max, w));
	ok = true;
	return true;
}


bool Solver::getRange(
	const Query& query,
	std::pair< ref<Expr>, ref<Expr> >& ret )
{
	ref<Expr>		e(query.expr);
	Expr::Width		w = e->getWidth();
	bool			ok;
	uint64_t		bits, min, max;

	if (fastGetRange(query, ret, ok))
		return ok;

	std::cerr << "HELLO QUERY: ";
	query.print(std::cerr);

	if (!getUsefulBits(query, bits))
		return false;

	std::cerr << "USEFUL BITS: " << bits << '\n';

	if (!getRangeMin(query, bits, min))
		return false;

	std::cerr << "GOT MIN: " << min << '\n';

	if (!getRangeMax(query, bits, min, max))
		return false;

	std::cerr << "GOT MAX: " << min << '\n';

	ret = std::make_pair(MK_CONST(min, w), MK_CONST(max, w));
	return true;
}

static bool getImpliedMin(
	Solver*		s,
	const Query&	q,
	uint64_t	pivot,
	uint64_t&	min)
{
	unsigned	w(q.expr->getWidth());
	ref<Expr>	hi_e(MK_ULE(q.expr, MK_CONST(pivot, w)));
	ref<Expr>	conclusion;
	uint64_t	lo, hi;

	conclusion = BinaryExpr::Fold(
		Expr::And,
		q.constraints.begin(),
		q.constraints.end());

	hi = pivot;
	lo = 0;
	while (lo < hi) {
		ConstraintManager	cm;
		Query			cur_q(cm, 0);
		ref<Expr>	premise;
		uint64_t	mid;
		bool		mbt;

		mid = lo + (hi - lo)/2;
		cm.addConstraint(MK_UGE(q.expr, MK_CONST(mid, w)));
		cm.addConstraint(hi_e);

		if (s->mustBeTrue(cur_q.withExpr(conclusion), mbt) == false)
			return false;

		if (mbt) {
			/* (mid <= x <= pivot) => mid can go lower*/
			hi = mid;
		} else
			lo = mid+1;
	}

	min = lo;
	return true;
}

static bool getImpliedMax(
	Solver* s, const Query& q, uint64_t pivot, uint64_t &max)
{
	unsigned	w(q.expr->getWidth());
	ref<Expr>	hi_e(MK_UGE(q.expr, MK_CONST(pivot, w)));
	ref<Expr>	conclusion;
	uint64_t	lo, hi;

	conclusion = BinaryExpr::Fold(
		Expr::And,
		q.constraints.begin(),
		q.constraints.end());

	hi = bits64::maxValueOfNBits(w);
	lo = pivot;
	while (lo < hi) {
		ConstraintManager	cm;
		Query			cur_q(cm, 0);
		ref<Expr>	premise;
		uint64_t	mid;
		bool		mbt;

		mid = lo + (hi - lo)/2;
		cm.addConstraint(MK_ULE(q.expr, MK_CONST(mid, w)));
		cm.addConstraint(hi_e);

		if (s->mustBeTrue(cur_q.withExpr(conclusion), mbt) == false)
			return false;

		/* (pivot <= x <= mid) is valid => range too small */
		if (mbt)
			lo = mid+1;
		else
			hi = mid;
	}

	if (lo == hi)
		max = (hi == pivot) ? pivot : hi - 1;
	else
		max = hi;

	return true;
}

/* get a range where query.e in [a,b] => query.constraints */
bool Solver::getImpliedRange(
	const Query&	query,
	uint64_t	pivot,
	std::pair<uint64_t, uint64_t>& ret)
{
	uint64_t		min, max;
	bool			ok;
	std::pair< ref<Expr>, ref<Expr> > ret_e;

	if (fastGetRange(query, ret_e, ok)) {
		ret.first = cast<ConstantExpr>(ret_e.first)->getZExtValue();
		ret.second = cast<ConstantExpr>(ret_e.second)->getZExtValue();
		return ok;
	}

	if (!getImpliedMin(this, query, pivot, min))
		return false;

	if (!getImpliedMax(this, query, pivot, max))
		return false;

	std::cerr << "MIN: " << min << ". MAX: " << max << '\n';
	ret = std::make_pair(min, max);
	return true;
}

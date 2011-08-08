#include <assert.h>
#include "static/Sugar.h"
#include "BoolectorSolver.h"
#include "klee/Constraints.h"
#include "klee/SolverStats.h"
#include "klee/TimerStatIncrementer.h"
#include <algorithm>
#include <iostream>

extern "C"
{
#include "boolector.h"
}

using namespace klee;

BoolectorSolver::BoolectorSolver(void)
: TimedSolver(new BoolectorSolverImpl())
{}

BoolectorSolver::~BoolectorSolver(void) {}


BoolectorSolverImpl::BoolectorSolverImpl(void)
{
	btor = boolector_new();
	boolector_enable_inc_usage(btor);	/* for assume() */
	boolector_enable_model_gen(btor);	/* for initial values */
}

BoolectorSolverImpl::~BoolectorSolverImpl(void)
{
	boolector_delete(btor);
}

void BoolectorSolverImpl::assumeConstraints(const Query& q)
{
	foreach (it, q.constraints.begin(), q.constraints.end()) {
		BtorExp		*constraint_exp;
		constraint_exp = klee2btor(*it);
		boolector_assume(btor, constraint_exp);
	}
}

bool BoolectorSolverImpl::computeSat(const Query& q)
{
	bool isSat = isSatisfiable(q);
	freeBtorExps();
	return isSat;
}

bool BoolectorSolverImpl::isSatisfiable(const Query& q)
{
	TimerStatIncrementer	t(stats::queryTime);
	bool			hasSolution;
	int			sat_rc;

	++stats::queries;
	++stats::queryCounterexamples;

	assumeConstraints(q);

	boolector_assume(btor, klee2btor(q.expr));

	sat_rc = boolector_sat(btor);
	hasSolution = (sat_rc == BOOLECTOR_SAT);

	if (hasSolution) {
		++stats::queriesValid;
	} else {
		++stats::queriesInvalid;
	}

	return hasSolution;
}

bool BoolectorSolverImpl::computeInitialValues(
	const Query& query,
	const std::vector<const Array*> &objects,
	std::vector< std::vector<unsigned char> > &values)
{
	bool	hasSolution;

	/*
	 * The query.expr is meant to contradict the model since
	 * STP works by building counter examples.
	 *
	 * if query expression contradicts model,
	 * 	we have a solution but an invalid model
	 * Hence, 'not' the query so that contradiction => valid model
	 *
	 * Very confusing semantics here. Thanks a lot guys.
	 */
	//hasSolution = isSatisfiable(query.negateExpr());
	hasSolution = isSatisfiable(query.negateExpr());
	if (!hasSolution)
		goto done;

	/* build the results */
	foreach (it, objects.begin(), objects.end()) {
		const Array			*arr = *it;
		std::vector<unsigned char>	obj_vals(arr->mallocKey.size);

		for (	unsigned offset = 0;
			offset < arr->mallocKey.size;
			offset++)
		{
			obj_vals[offset] = getArrayValue(arr, offset);
		}
		values.push_back(obj_vals);
	}

done:
	freeBtorExps();
	return hasSolution;
}

uint8_t BoolectorSolverImpl::getArrayValue(const Array *root, unsigned index)
{
	BtorExp	*readexp, *idx;
	uint8_t	ret;
	char	*assignment;

	idx = boolector_unsigned_int(btor, index, 32);
	readexp = boolector_read (btor, getInitialArray(root), idx);
	assignment = boolector_bv_assignment(btor, readexp);
	assert (assignment != NULL && "Could not get assignment for readexp");

	boolector_release(btor, readexp);
	boolector_release(btor, idx);

	ret = 0;
	for (int i = 0; i < 8; i++) {
		ret <<= 1;
		if (assignment[i] == '1') ret |= 1;
	}

	boolector_free_bv_assignment (btor, assignment);
	return ret;
}

void BoolectorSolverImpl::freeBtorExps(void)
{
	/* reset arrays stored in expressions */
	foreach (it, array_list.begin(), array_list.end())
		*(*it) = NULL;

	foreach (it, exp_set.begin(), exp_set.end())
		boolector_release(btor, *it);

	array_list.clear();
	exp_set.clear();
}

static int getLog2(unsigned int w)
{
	int	ret;
	ret = 0;
	while (w >> ret) ret++;
	return ret-1;
}

/* XXX use explicit stack if this breaks */
BtorExp* BoolectorSolverImpl::klee2btor(const ref<Expr>& e)
{
	int		width;
	BtorExp		*ret = NULL;

	++stats::queryConstructs;

	switch (e->getKind()) {
	case Expr::Constant: {
		ConstantExpr *CE = cast<ConstantExpr>(e);
		width = CE->getWidth();

		// Fast paths
		if (width == 1) {
			ret = CE->isTrue() ?
				boolector_true(btor) :
				boolector_false(btor);
			goto done;
		} else if (width <= 32) {
			ret = boolector_unsigned_int(
				btor, CE->getZExtValue(width), width);
			goto done;
		}

		ref<ConstantExpr> Tmp = CE;
		ret = boolector_unsigned_int(
			btor, Tmp->Extract(0, 32)->getZExtValue(), 32);
		for (unsigned i = (width / 32) - 1; i; --i) {
			BtorExp		*exp_part;

			Tmp = Tmp->LShr(ConstantExpr::alloc(32, Tmp->getWidth()));
			exp_part = boolector_unsigned_int(
				btor,
				Tmp->Extract(0, 32)->getZExtValue(),
				std::min(32U, Tmp->getWidth()));

			exp_set.insert(ret);
			exp_set.insert(exp_part);
			ret = boolector_concat(btor, exp_part, ret);
		}
		goto done;
	}

	// Special
	case Expr::NotOptimized: {
		NotOptimizedExpr *noe = cast<NotOptimizedExpr>(e);
		return klee2btor(noe->src);
	}

	case Expr::Read: {
		ReadExpr *re = cast<ReadExpr>(e);
		ret = boolector_read(
			btor,
			getArrayForUpdate(re->updates.root, re->updates.head),
			klee2btor(re->index));
		goto done;
	}

	case Expr::Select: {
		SelectExpr *se = cast<SelectExpr>(e);
		BtorExp*	cond = klee2btor(se->cond);
		BtorExp*	tExpr = klee2btor(se->trueExpr);
		BtorExp*	fExpr = klee2btor(se->falseExpr);

		ret = boolector_cond (btor, cond, tExpr, fExpr);
		goto done;
	}

	case Expr::Concat: {
		ConcatExpr *ce = cast<ConcatExpr>(e);
		unsigned numKids = ce->getNumKids();

		ret = klee2btor(ce->getKid(numKids-1));
		for (int i=numKids-2; i>=0; i--) {
			exp_set.insert(ret);
			ret = boolector_concat(
				btor,
				klee2btor(ce->getKid(i)),
				ret);
		}
		goto done;
	}

	case Expr::Extract: {
		ExtractExpr *ee = cast<ExtractExpr>(e);
		BtorExp		*src;

		src = klee2btor(ee->expr);
		width = ee->getWidth();
		ret = boolector_slice(btor, src, ee->offset+width-1, ee->offset);
		goto done;
	}

	// Casting
	case Expr::ZExt: {
		int		srcWidth;
		CastExpr	*ce = cast<CastExpr>(e);
		BtorExp		*src = klee2btor(ce->src);

		srcWidth = boolector_get_width(btor, src);
		width = ce->getWidth();
		if (srcWidth == 1) {
			ret = boolector_cond(
				btor, src, getOnes(width), getZeros(width));
			goto done;
		}

		assert (width >= srcWidth);
		ret = boolector_uext(btor, src, width-srcWidth);
		goto done;
	}

	case Expr::SExt: {
		int srcWidth;
		CastExpr *ce = cast<CastExpr>(e);
		BtorExp *src = klee2btor(ce->src);

		srcWidth = boolector_get_width(btor, src);
		width = ce->getWidth();
		if (srcWidth==1) {
			return boolector_cond(
				btor, src,
				getOnes(width),
				getZeros(width));
			goto done;
		}
		assert (width >= srcWidth);
		ret = boolector_sext(btor, src, width-srcWidth);
		goto done;
	}

	// Arithmetic
#define ARITH_EXPR(x, y, z)						\
	case Expr::x: {							\
		int		width_right;				\
		y*		ae = cast<y>(e);			\
		BtorExp*	left = klee2btor(ae->left);		\
		BtorExp*	right = klee2btor(ae->right);		\
		width = boolector_get_width(btor, left);		\
		width_right = boolector_get_width(btor, right);		\
		assert (width == width_right && "uncanonicalized "#x);	\
		ret = z(btor, left, right);				\
		goto done; }
#define BIT_EXPR(x,y,z)							\
	case Expr::x: {							\
		y*		ae = cast<y>(e);			\
		BtorExp*	left = klee2btor(ae->left);		\
		BtorExp*	right = klee2btor(ae->right);		\
		ret = z(btor, left, right);				\
		goto done; }

#define SHIFT_EXPR(x, y, z, w)						\
	case Expr::x: {							\
		y*		ae = cast<y>(e);			\
		int		w_right, w_l_log2;			\
		BtorExp*	left = klee2btor(ae->left);		\
		BtorExp*	right = klee2btor(ae->right);		\
		width = boolector_get_width(btor, left);		\
		w_right = boolector_get_width(btor, right);		\
		w_l_log2 = getLog2(width);				\
		if (w_l_log2 == getLog2(width-1)) {			\
			/* not power of 2, extend */			\
			int	old_width = width;			\
			w_l_log2++;					\
			width = 1ULL << w_l_log2;			\
			left = w(btor, left, width - old_width);	\
			exp_set.insert(left);				\
			std::cerr << 					\
				boolector_get_width(btor, left) << '\n';\
		}							\
		assert (w_l_log2 <= w_right && "TOO WIDE SHIFT");	\
		if (w_l_log2 != w_right) {				\
			right = boolector_slice (btor, right, w_l_log2-1, 0); \
			assert (boolector_get_width(btor, right) == w_l_log2); \
			exp_set.insert(right);			\
		}							\
		ret = z(btor, left, right);				\
		goto done; }


	ARITH_EXPR(Add, AddExpr, boolector_add)
	ARITH_EXPR(Sub, SubExpr, boolector_sub)
	ARITH_EXPR(Mul, MulExpr, boolector_mul)
	ARITH_EXPR(UDiv, UDivExpr, boolector_udiv)
	ARITH_EXPR(SDiv, SDivExpr, boolector_sdiv)
	ARITH_EXPR(URem, URemExpr, boolector_urem)
	ARITH_EXPR(SRem, SRemExpr, boolector_srem)

	// Bitwise
	case Expr::Not: {
		NotExpr *ne = cast<NotExpr>(e);
		BtorExp	*expr = klee2btor(ne->expr);
		ret = boolector_not(btor, expr);
		goto done;
	}

	BIT_EXPR(And, AndExpr, boolector_and)
	BIT_EXPR(Or, OrExpr, boolector_or)
	BIT_EXPR(Xor, XorExpr, boolector_xor)
	SHIFT_EXPR(Shl, ShlExpr, boolector_sll, boolector_uext)
	SHIFT_EXPR(LShr, LShrExpr, boolector_srl, boolector_uext)
	SHIFT_EXPR(AShr, AShrExpr, boolector_sra, boolector_sext)

	// Comparison
	case Expr::Eq: {
	EqExpr *ee = cast<EqExpr>(e);
	BtorExp *left = klee2btor(ee->left);
	BtorExp *right = klee2btor(ee->right);
	width = boolector_get_width(btor, left);
	if (width == 1) {
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(ee->left)) {
			if (CE->isTrue()) return right;
			ret = boolector_not(btor, right);
		} else {
			ret = boolector_iff(btor, left, right);
		}
	} else {
		ret = boolector_eq(btor, left, right);
	}
	goto done;
	}

#define CMP_EXPR(x,y,z)						\
	case Expr::x: {						\
	y *ue = cast<y>(e);					\
	BtorExp* left = klee2btor(ue->left);			\
	BtorExp* right = klee2btor(ue->right);			\
	ret = z(btor, left, right);				\
	goto done; }

	CMP_EXPR(Ult, UltExpr, boolector_ult)
	CMP_EXPR(Ule, UleExpr, boolector_ulte)
	CMP_EXPR(Slt, SltExpr, boolector_slt)
	CMP_EXPR(Sle, SleExpr, boolector_slte)

	// unused due to canonicalization
	#if 0
	case Expr::Ne:
	case Expr::Ugt:
	case Expr::Uge:
	case Expr::Sgt:
	case Expr::Sge:
	#endif

	default:
		assert(0 && "unhandled Expr type");
		ret = boolector_true(btor);
	}

done:
	exp_set.insert(ret);
	return ret;
}

BtorExp* BoolectorSolverImpl::getArrayForUpdate(
	const Array *root, const UpdateNode *un)
{
	BtorExp	*ret;

	if (!un) return getInitialArray(root);

	if (un->btorArray) return static_cast<BtorExp*>(un->btorArray);

	// FIXME: This really needs to be non-recursive.
	ret = boolector_write(
		btor,
		getArrayForUpdate(root, un->next),
		klee2btor(un->index),
		klee2btor(un->value));

	exp_set.insert(ret);

	array_list.push_back(&un->btorArray);
	un->btorArray = ret;

	return ret;
}

BtorExp* BoolectorSolverImpl::getInitialArray(const Array *root)
{
	// STP uniques arrays by name, so we make sure the name is unique by
	// including the address.
	char		buf[32];
	BtorExp		*ret;

	if (root->btorInitialArray)
		return static_cast<BtorExp*>(root->btorInitialArray);

	sprintf(buf, "%s_%p", root->name.c_str(), (void*) root);
	ret = buildArray(buf, 32, 8);
	if (!root->isConstantArray()) goto done;

	assert (root->mallocKey.size);
	for (unsigned i = 0, e = root->mallocKey.size; i != e; ++i) {
		ret = boolector_write(
			btor,
			ret,
			klee2btor(ConstantExpr::alloc(i, root->getDomain())),
			klee2btor(root->getValue(i)));

		exp_set.insert(ret);
	}

done:
	array_list.push_back(&root->btorInitialArray);
	root->btorInitialArray = ret;
	return ret;
}

BtorExp* BoolectorSolverImpl::buildArray(
	const char *name, unsigned indexWidth, unsigned valueWidth)
{
	BtorExp*	ret;
	ret = boolector_array(btor, valueWidth, indexWidth, name);
	exp_set.insert(ret);
	return ret;
}

BtorExp* BoolectorSolverImpl::getZeros(unsigned w)
{
	BtorExp*	ret;
	ret = boolector_zero(btor, w);
	exp_set.insert(ret);
	return ret;
}

BtorExp* BoolectorSolverImpl::getOnes(unsigned w)
{
	BtorExp*	ret;
	ret = boolector_ones(btor, w);
	exp_set.insert(ret);
	return ret;
}

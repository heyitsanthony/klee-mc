#include "Z3Solver.h"
#include "static/Sugar.h"
#include "klee/Constraints.h"
#include "klee/TimerStatIncrementer.h"
#include "klee/SolverStats.h"
#include <stdio.h>
#include <iostream>

using namespace klee;

#define Z3_1BIT_CONST(v) Z3_mk_unsigned_int(z3_ctx, v, Z3_mk_bv_sort(z3_ctx, 1))
#define Z3_INT_CONST(v,w)	_Z3_INT_CONST(\
	z3_ctx, v, (w == 1) ? NULL : getSort(w))
static Z3_ast _Z3_INT_CONST(Z3_context z3_ctx, uint64_t v, Z3_sort s)
{
	if (!s) {
		return (v) ? Z3_mk_true(z3_ctx) : Z3_mk_false(z3_ctx);
	}
	return Z3_mk_unsigned_int(z3_ctx, v, s);
}

Z3Solver::Z3Solver(void)
: TimedSolver(new Z3SolverImpl())
{}

Z3Solver::~Z3Solver(void) {}

Z3SolverImpl::Z3SolverImpl(void)
: z3_ctx(NULL)
{
	z3_cfg = Z3_mk_config();
	Z3_set_param_value(z3_cfg, "MODEL", "true");
	Z3_set_param_value(z3_cfg, "MODEL_PARTIAL", "false");
	Z3_set_param_value(z3_cfg, "MODEL_COMPLETION", "true");
	Z3_set_param_value(z3_cfg, "ELIM_TERM_ITE", "true");
}

Z3SolverImpl::~Z3SolverImpl(void)
{
	Z3_del_config(z3_cfg);
}

bool Z3SolverImpl::computeSat(const Query& q)
{
	TimerStatIncrementer	t(stats::queryTime);
	Z3_lbool	rc;
	bool		ret;

	z3_ctx = Z3_mk_context(z3_cfg);

	assumeConstraints(q);
	addConstraint(q.expr);

	++stats::queries;
	rc = Z3_check(z3_ctx);
	ret = wasSat(rc);

	cleanup();
	return ret;
}

bool Z3SolverImpl::wasSat(Z3_lbool rc)
{
	switch (rc) {
	case Z3_L_FALSE:
		++stats::queriesInvalid;
		break;
	case Z3_L_TRUE:
		++stats::queriesValid;
		return true;
	case Z3_L_UNDEF:
		std::cerr << "Z3_L_UNDEF!\n";
		failQuery();
		break;
	}

	return false;
}

void Z3SolverImpl::cleanup(void)
{
	z3_sort_cache.clear();
	foreach (it, z3_array_ptrs.begin(), z3_array_ptrs.end())
		*(*it) = NULL;
	z3_array_ptrs.clear();
	Z3_del_context(z3_ctx);
}

static void puke(Z3_error_code e)
{
	fprintf(stderr, "ARGHHHH %d %d\n", e, Z3_OK);
	/* trigger segfault handler for poison cache */
	*((char*)1) = 0;
}

bool Z3SolverImpl::computeInitialValues(
	const Query& q,
	const std::vector<const Array*> &objects,
	std::vector< std::vector<unsigned char> > &values)

{
	TimerStatIncrementer	t(stats::queryTime);
	Z3_lbool		rc;
	Z3_model		model;
	bool			isSat;

	z3_ctx = Z3_mk_context(z3_cfg);
	//Z3_toggle_warning_messages(true);
	//Z3_trace_to_stderr(z3_ctx);
	Z3_set_error_handler(z3_ctx, &puke);

	assumeConstraints(q);

	addConstraint(Expr::createIsZero(q.expr));

	++stats::queries;
	rc = Z3_check_and_get_model(z3_ctx, &model);

	isSat = wasSat(rc);
	if (!isSat) goto done;

	/* load up the objects */
	foreach (it, objects.begin(), objects.end()) {
		const Array			*arr = *it;
		std::vector<unsigned char>	obj_vals(arr->mallocKey.size);

		for (	unsigned offset = 0;
			offset < arr->mallocKey.size;
			offset++)
		{
			obj_vals[offset] = getArrayValue(model, arr, offset);
		}
		values.push_back(obj_vals);
	}

	Z3_del_model(z3_ctx, model);

done:
	cleanup();
	return isSat;
}

void Z3SolverImpl::addConstraint(const ref<Expr>& klee_cnstr)
{
	Z3_ast		cnstr;
	Z3_sort		sort;
	Z3_sort_kind	kind;

	cnstr = klee2z3(klee_cnstr);
	sort = Z3_get_sort(z3_ctx, cnstr);
	kind = Z3_get_sort_kind(z3_ctx, sort);
	if (kind != Z3_BOOL_SORT) {
		assert (kind == Z3_BV_SORT && "IF NOT BV OR BOOL, THEN WHAT?");
		assert (Z3_get_bv_sort_size(z3_ctx, sort) == 1);
		cnstr = Z3_mk_eq(z3_ctx, cnstr, Z3_1BIT_CONST(1));
	}

	Z3_assert_cnstr(z3_ctx, cnstr);
}

void Z3SolverImpl::assumeConstraints(const Query& q)
{
	foreach (it, q.constraints.begin(), q.constraints.end()) {
		addConstraint(*it);
	}
}

Z3_sort Z3SolverImpl::getSort(unsigned w)
{
	Z3_sort	s;

	s = z3_sort_cache[w];
	if (s == NULL) {
		s = (w == 1) ?
			Z3_mk_bool_sort(z3_ctx) :
			Z3_mk_bv_sort(z3_ctx, w);
		z3_sort_cache[w] = s;
	}

	return s;
}

Z3_ast Z3SolverImpl::klee2z3(const ref<Expr>& e)
{
	Z3_ast		ret = NULL;
	unsigned 	width;

	++stats::queryConstructs;

	switch (e->getKind()) {
	case Expr::Constant: {
		ConstantExpr	*CE = cast<ConstantExpr>(e);

		width = CE->getWidth();

		// Fast path
		if (width <= 32) {
			ret = Z3_INT_CONST(CE->getZExtValue(width), width);
			break;
		}

		/* concat at 32 bits a pop */
		ref<ConstantExpr> Tmp = CE;
		ret = Z3_INT_CONST(Tmp->Extract(0, 32)->getZExtValue(), 32);
		for (unsigned i = (width / 32) - 1; i; --i) {
			Tmp = Tmp->LShr(ConstantExpr::alloc(32, Tmp->getWidth()));
			ret = Z3_mk_concat(
				z3_ctx,
				Z3_INT_CONST(
					Tmp->Extract(0, 32)->getZExtValue(),
					std::min(32U, Tmp->getWidth())),
				ret);
		}
		break;
	}

	// Special
	case Expr::NotOptimized: {
		NotOptimizedExpr *noe = cast<NotOptimizedExpr>(e);
		return klee2z3(noe->src);
	}

	case Expr::Read: {
		ReadExpr *re = cast<ReadExpr>(e);
		ret = Z3_mk_select(
			z3_ctx,
			getArrayForUpdate(re->updates.root, re->updates.head),
			klee2z3(re->index));
		break;
	}

	case Expr::Select: {
		SelectExpr	*se = cast<SelectExpr>(e);
		Z3_ast		cond = klee2z3(se->cond);
		Z3_ast		tExpr = klee2z3(se->trueExpr);
		Z3_ast		fExpr = klee2z3(se->falseExpr);

		ret = Z3_mk_ite(z3_ctx, cond, tExpr, fExpr);
		break;
	}

	case Expr::Concat: {
		ConcatExpr	*ce = cast<ConcatExpr>(e);
		unsigned	numKids = ce->getNumKids();

		ret = klee2z3(ce->getKid(numKids-1));
		for (int i=numKids-2; i>=0; i--) {
			ret = Z3_mk_concat(
				z3_ctx, klee2z3(ce->getKid(i)), ret);
		}
		break;
	}

	case Expr::Extract: {
		ExtractExpr	*ee = cast<ExtractExpr>(e);
		width = ee->getWidth();
		ret = Z3_mk_extract(
			z3_ctx,
			ee->offset+width-1, ee->offset, klee2z3(ee->expr));
		break;
	}

#define EXT_EXPR(x,y)						\
	case Expr::x: {						\
		unsigned	srcWidth;			\
		CastExpr	*ce = cast<CastExpr>(e);	\
		Z3_ast		src = klee2z3(ce->src);		\
		if (Z3_get_sort_kind(				\
			z3_ctx,					\
			Z3_get_sort(z3_ctx, src)) == Z3_BOOL_SORT)	\
			src = Z3_mk_ite(			\
				z3_ctx,				\
				src,				\
				Z3_1BIT_CONST(1),		\
				Z3_1BIT_CONST(0));		\
		srcWidth = ce->src->getWidth();			\
		width = ce->getWidth();				\
		ret = y(z3_ctx, width-srcWidth, src);		\
		break;						\
	}

	EXT_EXPR(ZExt, Z3_mk_zero_ext)
	EXT_EXPR(SExt, Z3_mk_sign_ext)

	// Arithmetic
#define ARITH_EXPR(x, y, z)						\
	case Expr::x: {							\
		unsigned	width_right;				\
		y*		ae = cast<y>(e);			\
		width = ae->left->getWidth();				\
		width_right = ae->right->getWidth();			\
		assert (width == width_right && "uncanonicalized "#x);	\
		ret = z(z3_ctx, klee2z3(ae->left), klee2z3(ae->right));	\
		break; }
	ARITH_EXPR(Add, AddExpr, Z3_mk_bvadd)
	ARITH_EXPR(Sub, SubExpr, Z3_mk_bvsub)
	ARITH_EXPR(Mul, MulExpr, Z3_mk_bvmul)
	ARITH_EXPR(UDiv, UDivExpr, Z3_mk_bvudiv)
	ARITH_EXPR(SDiv, SDivExpr, Z3_mk_bvsdiv)
	ARITH_EXPR(URem, URemExpr, Z3_mk_bvurem)
	ARITH_EXPR(SRem, SRemExpr, Z3_mk_bvsrem)

#define BIT_EXPR(x,y,z)							\
	case Expr::x: {							\
		y*		ae = cast<y>(e);			\
		ret = z(z3_ctx, klee2z3(ae->left), klee2z3(ae->right));	\
		break; }
	// Bitwise
	case Expr::Not: {
		NotExpr *ne = cast<NotExpr>(e);
		ret = (e->getWidth() == Expr::Bool) ?
			Z3_mk_not(z3_ctx, klee2z3(ne->expr)) :
			Z3_mk_bvnot(z3_ctx, klee2z3(ne->expr));
		break;
	}
	BIT_EXPR(And, AndExpr, Z3_mk_bvand)
	BIT_EXPR(Or, OrExpr, Z3_mk_bvor)
	case Expr::Xor: {
		XorExpr*	ae = cast<XorExpr>(e);
		Z3_ast		ast_l, ast_r;
		ast_l = klee2z3(ae->left);
		ast_r = klee2z3(ae->right);
		ret = (e->getWidth() == Expr::Bool) ?
			Z3_mk_xor(z3_ctx, ast_l, ast_r) :
			Z3_mk_bvxor(z3_ctx, ast_l, ast_r);
		break;
	}

#define SHIFT_EXPR(x, y, z)						\
	case Expr::x: {							\
		y*		ae = cast<y>(e);			\
		assert (ae->left->getWidth() == ae->right->getWidth());	\
		ret = z(z3_ctx, klee2z3(ae->left), klee2z3(ae->right));	\
		break; }
	SHIFT_EXPR(Shl, ShlExpr, Z3_mk_bvshl)
	SHIFT_EXPR(LShr, LShrExpr, Z3_mk_bvlshr)
	SHIFT_EXPR(AShr, AShrExpr, Z3_mk_bvashr)

#define CMP_EXPR(x,y,z)						\
	case Expr::x: {						\
	y *ue = cast<y>(e);					\
	ret = z(z3_ctx, klee2z3(ue->left), klee2z3(ue->right));	\
	break; }

	case Expr::Eq: {
		EqExpr	*ee = cast<EqExpr>(e);
		ret = Z3_mk_eq(z3_ctx,
			boolify(klee2z3(ee->left)),
			boolify(klee2z3(ee->right)));
		break;
	}
	CMP_EXPR(Ult, UltExpr, Z3_mk_bvult)
	CMP_EXPR(Ule, UleExpr, Z3_mk_bvule)
	CMP_EXPR(Slt, SltExpr, Z3_mk_bvslt)
	CMP_EXPR(Sle, SleExpr, Z3_mk_bvsle)

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
		break;
	}

	return ret;
}

Z3_ast Z3SolverImpl::boolify(Z3_ast ast)
{
	Z3_sort		sort;
	Z3_sort_kind	kind;

	sort = Z3_get_sort(z3_ctx, ast);
	kind = Z3_get_sort_kind(z3_ctx, sort);
	if (kind != Z3_BOOL_SORT) {
		assert (kind == Z3_BV_SORT && "IF NOT BV OR BOOL, THEN WHAT?");
		if (Z3_get_bv_sort_size(z3_ctx, sort) == 1)
			ast = Z3_mk_eq(z3_ctx, ast, Z3_1BIT_CONST(1));
	}


	return ast;
}

static Z3_ast mk_var(Z3_context ctx, const char * name, Z3_sort ty)
{
	Z3_symbol   s  = Z3_mk_string_symbol(ctx, name);
	return Z3_mk_const(ctx, s, ty);
}

Z3_ast Z3SolverImpl::getArrayForUpdate(const Array *root, const UpdateNode *un)
{
	Z3_ast ret;

	if (!un) return getInitialArray(root);

	if (un->z3Array) return static_cast<Z3_ast>(un->z3Array);

	// FIXME: This really needs to be non-recursive.
	ret = Z3_mk_store(
		z3_ctx,
		getArrayForUpdate(root, un->next),
		klee2z3(un->index),
		klee2z3(un->value));

	z3_array_ptrs.push_back(&un->btorArray);
	un->z3Array = ret;

	return ret;
}

Z3_ast Z3SolverImpl::getInitialArray(const Array *root)
{
	char		buf[64];
	Z3_ast		ret;

	if (root->btorInitialArray)
		return static_cast<Z3_ast>(root->z3InitialArray);

	sprintf(buf, "%s_%p", root->name.c_str(), (void*) root);
	ret = mk_var(
		z3_ctx,
		root->name.c_str(),
		Z3_mk_array_sort(z3_ctx, getSort(32), getSort(8)));

	if (!root->isConstantArray()) goto done;

	assert (root->mallocKey.size);
	for (unsigned i = 0, e = root->mallocKey.size; i != e; ++i) {
    		ret = Z3_mk_store(
			z3_ctx,
			ret,
			klee2z3(ConstantExpr::alloc(i, root->getDomain())),
			klee2z3(root->getValue(i)));
	}

done:
	z3_array_ptrs.push_back(&root->btorInitialArray);
	root->z3InitialArray = ret;
	return ret;

}

uint8_t Z3SolverImpl::getArrayValue(
	Z3_model m,
	const Array *root,
	unsigned index)
{
	Z3_bool		rc;
	Z3_ast		readexp, v;
	unsigned int	ret;

	readexp = Z3_mk_select(
		z3_ctx,
		getInitialArray(root),
		Z3_INT_CONST(index, 32));

	rc = Z3_eval(z3_ctx, m, readexp, &v);
	assert (rc == Z3_TRUE && "Couldn't eval array read. Whoops!");

	rc = Z3_get_numeral_uint(z3_ctx, v, &ret);
	assert (rc == Z3_TRUE && "Couldn't get numeral for array read");
	assert (ret <= 0xff && "Array read 8-bit value > (2^8)-1");

	return (uint8_t)ret;
}

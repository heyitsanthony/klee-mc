#include <llvm/Support/CommandLine.h>
#include <iostream>


#include "OptBuilder.h"

using namespace klee;
using namespace llvm;

namespace {
  cl::opt<bool>
  ConstArrayOpt("eq-const-read-disjunct",
	 cl::init(false),
	 cl::desc("Convert (EqExpr cst rd-const-arr) to (Or (=ix) .. (=ix))"));
}

static int exact_log2(uint64_t x)
{
	int	k;

	if ((x & (x - 1)) != 0)
		return -1;

	k = -1;
	while (x) {
		x >>= 1;
		k++;
	}

	return k;
}

ref<Expr> OptBuilder::Concat(const ref<Expr> &l, const ref<Expr> &r)
{
	Expr::Width w = l->getWidth() + r->getWidth();

	// Fold concatenation of constants.
	if (ConstantExpr *lCE = dyn_cast<ConstantExpr>(l)) {
		if (lCE->isZero())
			return ZExtExpr::create(r, w);

		if (ConstantExpr *rCE = dyn_cast<ConstantExpr>(r))
			return lCE->Concat(rCE);

		if (ConcatExpr *ce_right = dyn_cast<ConcatExpr>(r)) {
			ConstantExpr *rCE;
			rCE = dyn_cast<ConstantExpr>(ce_right->getKid(0));
			if (rCE)
				return MK_CONCAT(
					lCE->Concat(rCE), ce_right->getKid(1));
		}
	}

	// Right-fold constants
	//
	// (concat (concat x rCE2) rCE) = (concat x (concat rCE2 rCE))
	if (ConstantExpr *rCE = dyn_cast<ConstantExpr>(l)) {
		if (ConcatExpr *lCon = dyn_cast<ConcatExpr>(r)) {
			ConstantExpr *rCE2;
			rCE2 = dyn_cast<ConstantExpr>(lCon->getKid(1));
			if (rCE2)
				return MK_CONCAT(
					lCon->getKid(0), rCE2->Concat(rCE));
		}

	}

	if (l->getKind() == Expr::Select && r->getKind() == Expr::Select) {
		SelectExpr	*s_l, *s_r;

		s_l = cast<SelectExpr>(l);
		s_r = cast<SelectExpr>(r);

		if (s_l->cond == s_r->cond) {
			return MK_SELECT(
				s_l->cond,
				MK_CONCAT(s_l->trueExpr, s_r->trueExpr),
				MK_CONCAT(s_l->falseExpr, s_r->falseExpr));
		}
	}

	ref<Expr>	ret_merge;
	ret_merge = mergeConcatSExt(l, r);
	if (!ret_merge.isNull())
		return ret_merge;

/* want to be able to translate (concat x bv0[k]) => (shl (zext x) k) */
#if 0
	if (ConstantExpr *rCE = dyn_cast<ConstantExpr>(r)) {
		if (rCE->isZero()) {
			return ShlExpr::create(
				ZExtExpr::create(l, w),
				ConstantExpr::create(rCE->getWidth(), w));
		}
	}

	if (ShlExpr *rShl = dyn_cast<ShlExpr>(r)) {
		ZExtExpr	*zKid = dyn_cast<ZExtExpr>(rShl->getKid(0));

		if (zKid) {
			return ShlExpr::create(
				ZExtExpr::create(
					ConcatExpr::create(l, zKid->getKid(0)),
					w),
				ZExtExpr::create(rShl->getKid(1), w));
		}
	}
#endif
	// Merge contiguous Extracts
	ret_merge = ConcatExpr::mergeExtracts(l, r);
	if (!ret_merge.isNull())
		return ret_merge;

	return ConcatExpr::alloc(l, r);
}

// create concat:
//  LHS = (sign_extend[7] ( extract[7:7] ( select ?e51  bv7[32] )))
//  RHS = (concat ( sign_extend[7] ( extract[7:7] ( select ?e51  bv7[32] ))) x)
//
//  RESULT: (concat (sign_extend[15] ...) x)
//
ref<Expr> OptBuilder::mergeConcatSExt(
	const ref<Expr>& l, const ref<Expr>& r) const
{
	const SExtExpr*		se_lhs;
	const ConcatExpr*	con_rhs;

	se_lhs = dyn_cast<SExtExpr>(l);
	if (se_lhs == NULL)
		return NULL;

	if (se_lhs->getKid(0)->getWidth() != 1)
		return NULL;

	con_rhs = dyn_cast<ConcatExpr>(r);
	if (con_rhs == NULL)
		return NULL;

	if (con_rhs->getKid(0) != l)
		return NULL;

	return ConcatExpr::create(
		SExtExpr::create(se_lhs->getKid(0), se_lhs->getWidth()*2),
		con_rhs->getKid(1));
}

ref<Expr> OptBuilder::Extract(const ref<Expr>& expr, unsigned off, Expr::Width w)
{
	unsigned kw = expr->getWidth();
	assert (w < 4096 && "A 4096 bit+ expression? Cool your jets.");
	assert (w > 0 && "invalid extract with negative width");
	assert (off + w <= kw && "invalid extract with excessive offset");

	if (w == kw)
		return expr;

	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr))
		return CE->Extract(off, w);

	// Extract(Concat)
	if (ConcatExpr *ce = dyn_cast<ConcatExpr>(expr)) {
		// if the extract skips the right side of the concat
		if (off >= ce->getRight()->getWidth())
			return ExtractExpr::create(
				ce->getLeft(),
				off - ce->getRight()->getWidth(), w);

		// if the extract skips the left side of the concat
		if (off + w <= ce->getRight()->getWidth())
			return ExtractExpr::create(ce->getRight(), off, w);

		// E(C(x,y)) = C(E(x), E(y))
		// XXX is this wise? it creates more expressions..
		return ConcatExpr::create(
			ExtractExpr::create(
				ce->getKid(0),
				0, w - ce->getKid(1)->getWidth() + off),
			ExtractExpr::create(
				ce->getKid(1),
				off, ce->getKid(1)->getWidth() - off));
	}

	if (off == 0) {
		// Extract(0,{Z,S}Ext(x)) = x
		if (CastExpr *ce = dyn_cast<CastExpr>(expr)) {
			if (ce->src->getWidth() >= w) {
				return ExtractExpr::create(ce->src, off, w);
			}
		} else if (BinaryExpr *be = dyn_cast<BinaryExpr>(expr)) {
			Expr::Kind rk = be->getKind();
			// E(x + y) = E(x) + E(y)
			if (	rk == Expr::Add || rk == Expr::Sub ||
				rk == Expr::And || rk == Expr::Or ||
				rk == Expr::Mul)
			{
				return BinaryExpr::create(
					rk,
					ExtractExpr::create(be->left, off, w),
					ExtractExpr::create(be->right, off, w));
			}
		}

		if (const ZExtExpr* ze = dyn_cast<ZExtExpr>(expr)) {
			// qemu gave me this gem:
			// extract[31:0] ( zero_extend[56] (select w8) )
			return ZExtExpr::create(ze->src, w);
		}

		// extract[7:0]
		// ( sign_extend[31] ( extract[7:7] ( select ?e41  bv7[32] )
		// =>
		// sext[7] (extract[7:7] ...)
		if (const SExtExpr* se = dyn_cast<SExtExpr>(expr))
			return SExtExpr::create(se->getKid(0), w);

		if (w == 1) {
			const XorExpr		*xe;
			const ConstantExpr	*ce;
			if (	(xe = dyn_cast<XorExpr>(expr)) &&
				(ce = dyn_cast<ConstantExpr>(xe->getKid(0))) &&
				ce->getWidth() <= 64 &&
				(ce->getZExtValue() & 1) == 0)
			{
				return ExtractExpr::create(
					xe->getKid(1), off, w);
			}
		}

	}


	// scumbag div with multiply
	// Unfortunately, 128 bit ops are EXPENSIVE so it's faster to do
	// a normal div!
	// ( extract[127:67]
	// 	(bvmul (concat bv0[64]  bv14757395258967641293[64] ) x) )
	if (off >= 67 && expr->getKind() == Expr::Mul) {
		const ConstantExpr	*ce;

		if ((ce = dyn_cast<ConstantExpr>(expr->getKid(0)))) {
			ref<ConstantExpr>	ce_lo, ce_hi;

			ce_lo = ce->Extract(0, 64);
			ce_hi = ce->Extract(64, ce->getWidth()-64);
			if (	ce_hi->isZero() &&
				ce_lo->getZExtValue() == 0xcccccccccccccccd)
			{
				ref<Expr>	x_div_10_mul8;

				x_div_10_mul8 = UDivExpr::create(
					ExtractExpr::create(
						expr->getKid(1),
						0,
						64),
					ConstantExpr::create(10, 64));
				return ExtractExpr::create(
					x_div_10_mul8, off - (64 + 3), w);
			}
		}
	}

	/* Extract(Extract) */
	if (expr->getKind() == Expr::Extract) {
		const ExtractExpr* ee = cast<ExtractExpr>(expr);
		return ExtractExpr::create(ee->expr, off+ee->offset, w);
	}

	if (expr->getKind() == Expr::ZExt) {
		const ZExtExpr* ze = cast<ZExtExpr>(expr);

		// Another qemu gem:
		// ( extract[31:8] ( zero_extend[56] ( select qemu_buf7 bv2[32])
		// So, rewrite extractions of zext 0's as 0

		if (off >= ze->src->getWidth())
			return ConstantExpr::create(0, w);

		// ( extract[31:8] ( zero_extend[32] bv4294967292[32]))
		if (off+w <= ze->src->getWidth())
			return ExtractExpr::create(ze->getKid(0), off, w);
	}

	if (expr->getKind() == Expr::SExt) {
		const SExtExpr* se = cast<SExtExpr>(expr);
		Expr::Width		active_w, sext_w;

		active_w = se->src->getWidth();
		if (off+w <= active_w) {
			return ExtractExpr::create(se->getKid(0), off, w);
		}

		assert (se->getWidth() >= active_w);
		sext_w = se->getWidth() - active_w;

		// from ntfsfix
		// extract[63:32] ( sign_extend[32]
		// (concat ( select reg4 bv3[32])
		// (concat ( select reg4 bv2[32])
		// (concat ( select reg4 bv1[32])
		// ( select reg4 bv0[32])
		// => (sign_extend[63] (extract[31:31] x))
		if (off >= active_w) {
			return MK_SEXT(MK_EXTRACT(se->src, active_w - 1, 1), w);
		}
	}

	if (expr->getKind() == Expr::Shl) {
		const ShlExpr	*shl_expr = cast<ShlExpr>(expr);
		ZExtExpr	*ze;
		ConstantExpr	*ce;

		ce = dyn_cast<ConstantExpr>(shl_expr->getKid(1));
		if (ce && ce->getWidth() <= 64) {
			unsigned	max_bit = off+w;
			unsigned	shl_bits = ce->getZExtValue();

			if (max_bit < shl_bits)
				return MK_CONST(0, w);
		}

		// from readelf
		// ( extract[7:0]
		//   (bvshl (zext[56] ( sel buf bv33[32])) bv48[64]))
		// >> works out to 0
		ze = dyn_cast<ZExtExpr>(shl_expr->getKid(0));
		if (ze && ce && ce->getWidth() <= 64) {
			unsigned int	active_begin, active_w;

			active_begin = ce->getZExtValue();
			active_w = ze->src->getWidth();

			/* active bytes start after extract */
			if (active_begin >= off+w) return MK_CONST(0, w);

			/* active bytes end before start of extract */
			if (active_begin+active_w <= off) return MK_CONST(0, w);
		}
	}

	if (	(	expr->getKind() == Expr::Or ||
			expr->getKind() == Expr::And ||
			expr->getKind() == Expr::Xor) &&
		//(off % 8 == 0) &&
		(expr->getKid(0)->getKind() == Expr::Concat ||
		 expr->getKid(1)->getKind() == Expr::Concat))
	{
		/* (extract[31:8]
			(bvor 	(concat ( select ?e71  bv59[32] )
				...
				( select ?e71  bv56[32] ))  bv1[32]))
		=> (bvor (...) bv0[24]) */

		return BinaryExpr::create(
			expr->getKind(),
			ExtractExpr::create(expr->getKid(0), off, w),
			ExtractExpr::create(expr->getKid(1), off, w));
	}

	if (const SelectExpr *se = dyn_cast<SelectExpr>(expr)) {
		if (	se->getKid(1)->getKind() == Expr::Constant &&
			se->getKid(2)->getKind() == Expr::Constant)
		{
			return SelectExpr::create(
				se->getKid(0),
				ExtractExpr::create(se->getKid(1), off, w),
				ExtractExpr::create(se->getKid(2), off, w));
		}
	}

	return ExtractExpr::alloc(expr, off, w);
}

ref<Expr> OptBuilder::Read(const UpdateList &ul, const ref<Expr>& index)
{
	// rollback index when possible...

	// sanity check for OoB read
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(index)) {
		if (CE->getZExtValue() >= ul.getRoot()->mallocKey.size) {
			Expr::errors++;
			std::cerr << "[Expr] Replaing OOB read with 0.\n";
			std::cerr << "[Expr] mallocKey.size="
				<< ul.getRoot()->mallocKey.size << '\n';
			std::cerr << "[Expr] CE=" <<  *CE << '\n';
			return ConstantExpr::create(0, 32);
		}
	}

	// XXX this doesn't really belong here... there are basically two
	// cases, one is rebuild, where we want to optimistically try various
	// optimizations when the index has changed, and the other is
	// initial creation, where we expect the ObjectState to have constructed
	// a smart UpdateList so it is not worth rescanning.

	const UpdateNode *un = ul.head;
	for (; un; un=un->next) {
		ref<Expr> cond = EqExpr::create(index, un->index);

		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(cond)) {
			if (CE->isTrue())
				return un->value;
		} else {
			break;
		}
	}

	return ReadExpr::alloc(ul, index);
}

ref<Expr> OptBuilder::Select(
	const ref<Expr>& c,
	const ref<Expr>& t,
	const ref<Expr>& f)
{
	Expr::Width kt = t->getWidth();

	assert (c->getWidth() == Expr::Bool && "type mismatch");
	assert (kt==f->getWidth() && "type mismatch");

	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(c))
		return CE->isTrue() ? t : f;

	if (t==f)
		return t;

	// c ? t : f  <=> (c and t) or (not c and f)
	if (kt == Expr::Bool) {
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(t)) {
			if (CE->isTrue())
				return MK_OR(c, f);
			return MK_AND(Expr::createIsZero(c), f);
		} else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(f)) {
			if (CE->isTrue())
				return MK_OR(Expr::createIsZero(c), t);
			return MK_AND(c, t);
		}
	}

	/* try to lift nested select exprs */
	if (const EqExpr* ee = dyn_cast<EqExpr>(c)) {
		// (ite (= c0 se:(ite cond t0 f0)) t1 f1)
		const SelectExpr	*se;

		if ((se = dyn_cast<SelectExpr>(ee->getKid(1)))) {
			// c0 != c1 != c2
			// (ite (eq c0 (ite c c1 c2)) t f) =>
			// (ite (false) t f) =>
			// f
			if (	se->getKid(1)->getKind() == Expr::Constant &&
				se->getKid(2)->getKind() == Expr::Constant &&
				ee->getKid(0)->getKind() == Expr::Constant)
			{
				if (	se->getKid(1) != ee->getKid(0) &&
					se->getKid(2) != ee->getKid(0))
				{
					return f;
				}
			}

			// (ite (= v0 (ite (cond) v1 v0) v1 v0)) =>
			// (ite (cond) v0 v1)
			if (	ee->getKid(0) == se->getKid(2) &&
				t == se->getKid(1) &&
				f == se->getKid(2))
			{
				return SelectExpr::create(
					se->getKid(0),
					se->getKid(2),
					se->getKid(1));
			}


		}

	}

	return SelectExpr::alloc(c, t, f);
}

ref<Expr> OptBuilder::ZExt(const ref<Expr> &e, Expr::Width w)
{
	unsigned kBits = e->getWidth();

	if (w == kBits) return e;

	// trunc
	if (w < kBits) return ExtractExpr::create(e, 0, w);

	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(e))
		return CE->ZExt(w);

#if 0
	/* In David's branch this only happens if 'optimize' is set;
	 * it's not clear to me reason why you not want to do this. */
	if (const SelectExpr *se = dyn_cast<SelectExpr>(e)) {
		std::vector<ref<Expr> > values(se->values.size());
		for (unsigned i = 0; i < se->values.size(); i++)
			values[i] = ZExtExpr::create(se->values[i], w);
		return SelectExpr::create(se->conds, values);
	}
#endif
	if (kBits == 1) {
		return MK_SELECT(e, MK_CONST(1, w), MK_CONST(0, w));
	}

	// NOTE:
	// ( zero_extend[32] (concat bv0[24] ( select qemu_buf7 bv2[32])
	// should now be folded concatexpr::create

	// Zext(Zext)
	if (e->getKind() == Expr::ZExt) {
		return ZExtExpr::alloc(e->getKid(0), w);
	}

	// there are optimizations elsewhere that deal with concatenations of
	// constants within their arguments, so we're better off concatenating 0
	// than using ZExt. ZExt(X, w) = Concat(0, X)
	//
	// But for now, don't do it.
	// return ConcatExpr::create(ConstantExpr::alloc(0, w - kBits), e);

	return ZExtExpr::alloc(e, w);
}

ref<Expr> OptBuilder::SExt(const ref<Expr> &e, Expr::Width w)
{
	unsigned kBits = e->getWidth();
	if (w == kBits)
		return e;

	// trunc
	if (w < kBits) return ExtractExpr::create(e, 0, w);

	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(e))
		return CE->SExt(w);

	// via qemu, again:
	// ( sign_extend[32] (concat bv0[24]  (select qemu_buf7 ...
	if (e->getKind() == Expr::Concat) {
		/* concat 0s into MSB implies that sign extension
		 * will always be a zero extension */
		const ConcatExpr *con = cast<ConcatExpr>(e);
		const ConstantExpr* CE;

		CE = dyn_cast<ConstantExpr>(con->getKid(0));
		if (CE && CE->isZero()) {
			assert (CE->getWidth() > 0);
			return ZExtExpr::create(con->getKid(1), w);
		}
	}

	if (e->getKind() == Expr::ZExt) {
	// sign_extend[32] ( zero_extend[24] ( select qemu_buf7 bv2[32])
		if (e->getKid(0)->getWidth() < e->getWidth()) {
			return ZExtExpr::create(e->getKid(0), w);
		}
	}

	if (e->getKind() == Expr::Select) {
		SelectExpr	*se = cast<SelectExpr>(e);

		if (	se->getKid(1)->getKind() == Expr::Constant &&
			se->getKid(2)->getKind() == Expr::Constant)
		{
			return SelectExpr::create(
				se->getKid(0),
				MK_SEXT(se->getKid(1), w),
				MK_SEXT(se->getKid(2), w));
		}
	}

	return SExtExpr::alloc(e, w);
}

static ref<Expr> AndExpr_create(Expr *l, Expr *r);
static ref<Expr> XorExpr_create(Expr *l, Expr *r);

static ref<Expr> EqExpr_createPartialR(Expr *l, const ref<ConstantExpr> &cr);
static ref<Expr> EqExpr_createPartialL(const ref<ConstantExpr> &cl, Expr *r);

static ref<Expr> AndExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r);
static ref<Expr> SubExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r);
static ref<Expr> XorExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r);

static ref<Expr> AddExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r)
{
	Expr::Width type = cl->getWidth();

	if (type==Expr::Bool)
		return XorExpr_createPartialR(cl, r);

	if (cl->isZero())
		return r;

	Expr::Kind rk = r->getKind();

	// A + (B+c) == (A+B) + c
	if (rk==Expr::Add && isa<ConstantExpr>(r->getKid(0)))
		return MK_ADD(MK_ADD(cl, r->getKid(0)), r->getKid(1));

	// A + (B-c) == (A+B) - c
	if (rk==Expr::Sub && isa<ConstantExpr>(r->getKid(0)))
		return MK_SUB(MK_ADD(cl, r->getKid(0)), r->getKid(1));

	return AddExpr::alloc(cl, r);
}

static ref<Expr> AddExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr)
{ return AddExpr_createPartialR(cr, l); }

static ref<Expr> AddExpr_create(Expr *l, Expr *r)
{
	Expr::Width type = l->getWidth();

	if (type == Expr::Bool)
		return XorExpr_create(l, r);

	Expr::Kind lk = l->getKind(), rk = r->getKind();

	// (k+a)+b = k+(a+b)
	if (lk==Expr::Add && isa<ConstantExpr>(l->getKid(0)))
		return MK_ADD(l->getKid(0), MK_ADD(l->getKid(1), r));

	// (k-a)+b = k+(b-a)
	if (lk==Expr::Sub && isa<ConstantExpr>(l->getKid(0)))
		return MK_ADD(l->getKid(0), MK_SUB(r, l->getKid(1)));

	// a + (k+b) = k+(a+b)
	if (rk==Expr::Add && isa<ConstantExpr>(r->getKid(0)))
		return MK_ADD(r->getKid(0), MK_ADD(l, r->getKid(1)));

	// a + (k-b) = k+(a-b)
	if (rk==Expr::Sub && isa<ConstantExpr>(r->getKid(0)))
		return MK_ADD(r->getKid(0), MK_SUB(l, r->getKid(1)));

/* this never seems to turn up anywhere in real life? */
#if 0
	if (*l == *r) {
		assert (0 == 1 && "ADD->SHL");
		return ShlExpr::create(
			l,
			ConstantExpr::create(1, l->getWidth()));
	}
#endif

	return AddExpr::alloc(l, r);
}

static ref<Expr> SubExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r)
{
	Expr::Width type = cl->getWidth();

	if (type==Expr::Bool)
		return XorExpr_createPartialR(cl, r);

	Expr::Kind rk = r->getKind();
	// A - (c+B) == (A-B) - c
	if (rk==Expr::Add && isa<ConstantExpr>(r->getKid(0)))
		return MK_SUB(MK_SUB(cl, r->getKid(0)), r->getKid(1));

	// A - (B-c) == (A-B) + c
	if (rk==Expr::Sub && isa<ConstantExpr>(r->getKid(0)))
		return MK_ADD(MK_SUB(cl, r->getKid(0)), r->getKid(1));

	/* NOTE: invalid optimization:
	 * (sub (zext x) (zext y)) != (sext (sub x y))) */

	return SubExpr::alloc(cl, r);
}

static ref<Expr> SubExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr)
{
	// l - c => l + (-c)
	return AddExpr_createPartial(l, cr->Neg());
}

static ref<Expr> SubExpr_create(Expr *l, Expr *r)
{
	Expr::Width type = l->getWidth();

	if (type == Expr::Bool) return XorExpr_create(l, r);

	if (*l == *r) return MK_CONST(0, type);

	Expr::Kind	lk = l->getKind();
	Expr::Kind	rk = r->getKind();

	// (k+a)-b = k+(a-b)
	if (lk == Expr::Add && isa<ConstantExpr>(l->getKid(0)))
		return MK_ADD(l->getKid(0), MK_SUB(l->getKid(1), r));

	// (k-a)-b = k-(a+b)
	if (lk == Expr::Sub && isa<ConstantExpr>(l->getKid(0)))
		return MK_SUB(l->getKid(0), MK_ADD(l->getKid(1), r));
	// a - (k+b) = (a-c) - k
	if (rk == Expr::Add && isa<ConstantExpr>(r->getKid(0)))
		return MK_SUB(MK_SUB(l, r->getKid(1)), r->getKid(0));

	// a - (k-b) = (a+b) - k
	if (rk == Expr::Sub && isa<ConstantExpr>(r->getKid(0)))
		return MK_SUB(MK_ADD(l, r->getKid(1)), r->getKid(0));

	/* NOTE: invalid optimization:
	 * (sub (zext x) (zext y)) != (sext (sub x y))) */

	return SubExpr::alloc(l, r);
}

static ref<Expr> MulExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r)
{
	Expr::Width type = cl->getWidth();

	if (type == Expr::Bool)
		return AndExpr_createPartialR(cl, r);

	if (cl->isOne())
		return r;

	if (cl->isZero())
		return cl;

	if (type <= 64) {
		int		k;

		/* mul_c = 2^k? */
		k = exact_log2(cl->getZExtValue());
		if (k != -1) {
			return MK_SHL(r, MK_CONST(k, type));
		}
	}


	return MulExpr::alloc(cl, r);
}

static ref<Expr> MulExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr)
{
	return MulExpr_createPartialR(cr, l);
}

static ref<Expr> MulExpr_create(Expr *l, Expr *r)
{
	Expr::Width type = l->getWidth();

	if (type == Expr::Bool)
		return AndExpr::alloc(l, r);

	return MulExpr::alloc(l, r);
}

static ref<Expr> AndExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr)
{
	if (cr->isAllOnes()) return l;
	if (cr->isZero()) return cr;

	if (ConcatExpr *cl = dyn_cast<ConcatExpr>(l)) {
		// AND(	Concat(x,y), N) <==>
		// 	Concat(And(x, N[...]), And(y, N[...:0]))
		return ConcatExpr::create(
			AndExpr::create(
				cl->getLeft().get(),
				cr->Extract(
					cl->getRight()->getWidth(),
					cl->getLeft()->getWidth())),
			AndExpr::create(
				cl->getRight().get(),
				cr->Extract(
					0,
					cl->getRight()->getWidth())));
	}

	// Lift extractions for (2^k)-1 bitmasks
	// (bvand (whatever) bv7[8])
	// into
	// zext[5] (extract[2:0] (extractwhatever))
	if (cr->getWidth() <= 64) {
		uint64_t	v;
		int		k;

		v = cr->getZExtValue();
		v++;	// v = 1...1 => v+1 = 2^k
		assert (v != 0 && "but isAllOnes() is false!");

		k = exact_log2(v);
		if (k != -1) {
			int	bits_set = 0;

			// 2^0 (excluded)
			// 2^1-1 => 1 bits 001
			// 2^2-1 => 2 bits 011
			// ...
			// 2^k - 1 => k bits
			bits_set = k;

			return MK_ZEXT(
				MK_EXTRACT(l, 0, bits_set), l->getWidth());
		}

	}

	// lift zero_extensions
	// (bvand ( zero_extend[24] ( select qemu_buf7 bv4[32]) ) bv7[32] )
	// into
	// zero_extend[24]  (bvand (select) bv7[8]))
	if (l->getKind() == Expr::ZExt) {
		ZExtExpr	*ze = static_cast<ZExtExpr*>(l);
		Expr::Width	cr_w, src_w;

		// BUG FIEND NOTE:
		// It's important to verify that the truncated constant
		// is equal to the normal constant!
		cr_w = cr->getWidth();
		src_w = ze->getKid(0)->getWidth();
		if (	cr_w <= 64 && src_w <= 64 &&
			cr->ZExt(src_w)->getZExtValue() == cr->getZExtValue())
		{
			return MK_ZEXT(
				MK_AND(cr->ZExt(src_w), ze->getKid(0)),
				ze->getWidth());
		}
	}

	return AndExpr::alloc(cr, l);
}
static ref<Expr> AndExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r)
{
	return AndExpr_createPartial(r, cl);
}

static ref<Expr> AndExpr_create(Expr *l, Expr *r)
{
	Expr	*t = NULL;
	if (*l == *r)
		return l;

	if (r->getKind() == Expr::And) {
		t = l;
		l = r;
		r = t;
	}

	/* try to reduce redundant AND chaining */
	if (l->getKind() == Expr::And) {
		if (*l->getKid(0) == *r || *l->getKid(1) == *r)
			return l;
	}

	if (t != NULL) {
		r = l;
		l = t;
	}

	if (l->getWidth() == Expr::Bool) {
		// a && !a = false
		if (*l == *Expr::createIsZero(r).get())
			return ConstantExpr::create(0, Expr::Bool);
	}

	CmpExpr *ce_left, *ce_right;
	ce_left = dyn_cast<CmpExpr>(l);
	ce_right = dyn_cast<CmpExpr>(r);
	if (ce_left && ce_right) {
		// (x <= y) & (y <= x) <==> x == y
		if (	((isa<UleExpr>(ce_left) && isa<UleExpr>(ce_right))
			|| (isa<SleExpr>(ce_left) && isa<SleExpr>(ce_right)))
			&& ce_left->left == ce_right->right
			&& ce_left->right == ce_right->left)
		{
			return EqExpr::create(ce_left->left, ce_left->right);
		}

		// (x < y) & (y < x) <==> false
		if (	((isa<UltExpr>(ce_left) && isa<UltExpr>(ce_right))
			|| (isa<SltExpr>(ce_left) && isa<SltExpr>(ce_right)))
			&& ce_left->left == ce_right->right
			&& ce_left->right == ce_right->left)
		{
			return ConstantExpr::create(0, Expr::Bool);
		}
	}

#if 0
	/* XXX this upsets the internal STP used by the test cases for some
	 * reason. Better disable it for now.
	/* (And (Eq c1 x) (Eq c2 y)) => (Eq (concat c1 c2) (concat x y)) */
	if (l->getKind() == Expr::Eq && r->getKind() == Expr::Eq) {
		const ConstantExpr	*l_ce, *r_ce;

		l_ce = dyn_cast<ConstantExpr>(l->getKid(0));
		r_ce = dyn_cast<ConstantExpr>(r->getKid(0));
		if (	l_ce && r_ce &&
			l_ce->getWidth() <= 32 &&
			l_ce->getWidth() == r_ce->getWidth())
		{
			return  ZExtExpr::create(
				EqExpr::create(
					ConcatExpr::create(
						l->getKid(0), r->getKid(0)),
					ConcatExpr::create(
						l->getKid(1), r->getKid(1))),
				l->getWidth());
		}
	}
#endif
	return AndExpr::alloc(l, r);
}

static ref<Expr> OrExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr)
{
	if (cr->isAllOnes())
		return cr;
	if (cr->isZero())
		return l;

	/* don't reapply */
	if (l->getKind() == Expr::Or) {
		const OrExpr		*o = static_cast<const OrExpr*>(l);
		if (o->getKid(0)->getKind() == Expr::Constant) {
			ref<ConstantExpr>	ce;
			ce = dyn_cast<ConstantExpr>(o->getKid(0));
			if (ce == cr)
				return l;
			return MK_OR(MK_OR(ce, cr), l->getKid(1));
		}
	}


	return OrExpr::alloc(cr, l);
}

static ref<Expr> OrExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r)
{ return OrExpr_createPartial(r, cl); }

static ref<Expr> OrExpr_factorZExt(Expr* l, Expr* r)
{
	// (bvor (zext[56] x) (zext[56] y))
	// => zext[56] (bvor x y)
	const ZExtExpr		*ze[2];
	ref<Expr>		e[2];
	const ConcatExpr	*cat;

	ze[0] = dyn_cast<ZExtExpr>(l);
	if (ze[0] == NULL)
		return NULL;

	ze[1] = dyn_cast<ZExtExpr>(r);
	if (ze[1] == NULL)
		return NULL;

	e[0] = ze[0]->src;
	e[1] = ze[1]->src;
	if (e[0]->getWidth() == e[1]->getWidth()) {
		return ZExtExpr::create(
			OrExpr::create(e[0], e[1]),
			ze[0]->getWidth());
	}

	/* canonicalize, e[0].w < e[1].w */
	if (e[0]->getWidth() > e[1]->getWidth()) {
		ref<Expr>	t(e[0]);
		e[0] = e[1];
		e[1] = t;
	}

	if (e[1]->getKind() != Expr::Concat)
		return NULL;

	cat = static_cast<const ConcatExpr*>(e[1].get());


	/* ze[0]->src (concat a (y-bits))	*/
	/* ze[1]->src = ((spare bits) y)	*/
	/* want to try to swap in x's high part into y's high part */
	if (	e[1]->getKid(1)->isZero() &&
		e[0]->getWidth() == e[1]->getKid(1)->getWidth())
	{
		return ZExtExpr::create(
			ConcatExpr::create(e[1]->getKid(0), e[0]),
			ze[0]->getWidth());
	}

	return NULL;
}

static ref<Expr> OrExpr_create(Expr *l, Expr *r)
{
	if (*l == *r)
		return l;

	if (l->getWidth() == Expr::Bool) {
		// a || !a = true
		if (*l == *Expr::createIsZero(r).get())
		      return ConstantExpr::create(1, Expr::Bool);
	}


	ref<Expr>	ret(OrExpr_factorZExt(l, r));
	if (!ret.isNull())
		return ret;

	return OrExpr::alloc(l, r);
}

static ref<Expr> XorExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r)
{
	if (cl->isZero())
		return r;

	if (cl->getWidth() == Expr::Bool)
		return EqExpr_createPartialL(MK_CONST(0, Expr::Bool), r);

	if (const SelectExpr *se = dyn_cast<SelectExpr>(r)) {
		if (	se->getKid(1)->getKind() == Expr::Constant &&
			se->getKid(2)->getKind() == Expr::Constant)
		{
			return MK_SELECT(
				se->getKid(0),
				MK_XOR(cl, se->getKid(1)),
				MK_XOR(cl, se->getKid(2)));
		}
	}

	return XorExpr::alloc(cl, r);
}

static ref<Expr> XorExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr)
{ return XorExpr_createPartialR(cr, l); }

static ref<Expr> XorExpr_create(Expr *l, Expr *r)
{
	if (*l == *r) {
		return ConstantExpr::create(0, l->getWidth());
	}
	return XorExpr::alloc(l, r);
}

static ref<Expr> UDivExpr_create(const ref<Expr> &l, const ref<Expr> &r)
{
	// r must be 1
	if (l->getWidth() == Expr::Bool)
		return l;

	return UDivExpr::alloc(l, r);
}

static ref<Expr> SDivExpr_create(const ref<Expr> &l, const ref<Expr> &r)
{
	// r must be 1
	if (l->getWidth() == Expr::Bool)
		return l;

	return SDivExpr::alloc(l, r);
}

static ref<Expr> URemExpr_create(const ref<Expr> &l, const ref<Expr> &r)
{
	const ZExtExpr	*ze_l, *ze_r;
	const ConstantExpr	*ce_r;

	// r must be 1
	if (l->getWidth() == Expr::Bool)
		return ConstantExpr::create(0, Expr::Bool);

	// special case: 0 % x = 0
	if (l->isZero())
		return l;


	if (	(ze_l = dyn_cast<ZExtExpr>(l)) &&
		(ze_r = dyn_cast<ZExtExpr>(r)))
	{
		int	l_zero_bits, r_zero_bits;

		l_zero_bits = l->getWidth() - l->getKid(0)->getWidth();
		r_zero_bits = r->getWidth() - r->getKid(0)->getWidth();
		if (l_zero_bits == r_zero_bits && l_zero_bits > 0) {
			return 	ZExtExpr::create(
					URemExpr::create(l->getKid(0), r->getKid(0)),
					ze_l->getWidth());

		}
	} else if (ze_l && (ce_r = dyn_cast<ConstantExpr>(r))) {
		int			l_zero_bits;
		int			kid_w;

		kid_w = l->getKid(0)->getWidth();
		l_zero_bits = l->getWidth() - kid_w;
		if (l_zero_bits > 0) {
			ref<ConstantExpr>	trunc_ce;

			trunc_ce = ce_r->Extract(l->getWidth()-l_zero_bits, l_zero_bits);

			if (trunc_ce->isZero()) {
				return 	ZExtExpr::create(
						URemExpr::create(
							l->getKid(0),
							ce_r->Extract(0, kid_w)),
						ze_l->getWidth());
			}
		}
	}

	return URemExpr::alloc(l, r);
}

static ref<Expr> SRemExpr_create(const ref<Expr> &l, const ref<Expr> &r)
{
	// r must be 1
	if (l->getWidth() == Expr::Bool)
		return ConstantExpr::create(0, Expr::Bool);

	return SRemExpr::alloc(l, r);
}

static ref<Expr> ShlExpr_create(const ref<Expr> &l, const ref<Expr> &r)
{
	// l & !r
	if (l->getWidth() == Expr::Bool)
		return AndExpr::create(l, Expr::createIsZero(r));

	if (l->getKind() == Expr::Shl)
		return ShlExpr::create(
			l->getKid(0),
			AddExpr::create(l->getKid(1), r));

	if (const ConstantExpr* ce = dyn_cast<ConstantExpr>(r)) {
		uint64_t		ce_val;

		/* the same optimizations can be done on large values,
		 * but it's a pain in the butt */
		if (ce->getWidth() > 64)
			return ShlExpr::alloc(l, r);

		ce_val = ce->getZExtValue();
		if (ce_val >= l->getWidth())
			return ConstantExpr::alloc(0, l->getWidth());

		if (ce_val == 0) return l;

		// ( bvshl
		//	( zero_extend[56] ( select readbuf6 bv2[32]))
		//	bv8[64] ))
		// =>
		// zext[48] (concat (select rbuf) bv0[8])
		if (const ZExtExpr* ze = dyn_cast<ZExtExpr>(l)) {
			ref<Expr>	shifted_bits;

			shifted_bits = MK_CONCAT(
				ze->src, MK_CONST(0, ce->getZExtValue()));

			return ZExtExpr::create(shifted_bits, ze->getWidth());
		}

		const ConcatExpr* cc = dyn_cast<ConcatExpr>(l);
		if (cc && (ce_val % 8 == 0)) {
			return ShlExpr::create(
				ZExtExpr::create(
					MK_EXTRACT(l, 0, l->getWidth() - ce_val),
					l->getWidth()),
				ConstantExpr::create(ce_val, l->getWidth()));
		}
	}

	return ShlExpr::alloc(l, r);
}

// Note: ashr (zext x) => lshr (zext x)
static ref<Expr> ShrExprZExt_create(const ref<Expr> &l, const ref<Expr> &r)
{
	ConstantExpr *ce = dyn_cast<ConstantExpr>(r);
	if (ce == NULL || ce->getWidth() > 64)
		return NULL;

	const ZExtExpr	*ze = dyn_cast<ZExtExpr>(l);
	if (ze == NULL)
		return NULL;

	Expr::Width	live_bits = ze->getKid(0)->getWidth();
	uint64_t	shift_bits = ce->getZExtValue();

	// bvshr
	//	( zero_extend[56] ( select qemu_buf7 bv6[32]) )
	//	bv3[64] ))
	//
	// into
	// zext[56] (bvshr (sel qemubuf) bv3[8])
	// into
	// zext[64-5] (extract[7:3]  (sel qemubuf))


	/* shifting off more bits than live => 0 */
	if (shift_bits >= live_bits)
		return ConstantExpr::alloc(0, ze->width);

	assert (shift_bits < live_bits);
	return ZExtExpr::create(
		ExtractExpr::create(
			ze->getKid(0), shift_bits, live_bits - shift_bits),
		ze->width);
}

static ref<Expr> LShrExpr_create(const ref<Expr> &l, const ref<Expr> &r)
{
	// l & !r
	if (l->getWidth() == Expr::Bool)
		return AndExpr::create(l, Expr::createIsZero(r));

	ref<Expr> ret(ShrExprZExt_create(l, r));
	if (!ret.isNull())
		return ret;

	const ConstantExpr* ce = dyn_cast<ConstantExpr>(r);
	if (ce && ce->getWidth() <= 64) {
		// Constant shifts can be rewritten into Extracts
		// I assume extracts are more desirable by virtue of
		// having fewer Expr parameters.
		uint64_t	off = ce->getZExtValue();

		if (off >= l->getWidth())
			return ConstantExpr::alloc(0, l->getWidth());

		return ZExtExpr::create(
			ExtractExpr::create(l, off, l->getWidth() - off),
			l->getWidth());
	}

	return LShrExpr::alloc(l, r);
}

static ref<Expr> AShrExpr_create(const ref<Expr> &l, const ref<Expr> &r)
{
	const ConstantExpr	*ce;
	uint64_t		w;

	w = l->getWidth();
	if (w == Expr::Bool)
		return l;

	ce = dyn_cast<ConstantExpr>(r);
	if (ce != NULL && ce->getWidth() <= 64) {
		uint64_t		shr_bits =  ce->getZExtValue();

		/* STP behavior here seems to be
		 * l < 0 => ~0
		 * l >= 0 => 0
		 * I can't think of a nice way of doing this without
		 * extra expressions, so punt
		 */
		if (shr_bits >= w)
			return AShrExpr::alloc(l, r);

		return SExtExpr::create(
			ExtractExpr::create(
				l,
				shr_bits,
				w - shr_bits),
			w);
	}

	ref<Expr> ret(ShrExprZExt_create(l, r));
	if (!ret.isNull())
		return ret;

	if (const ExtractExpr* ee = dyn_cast<ExtractExpr>(l)) {
		if (const ConstantExpr* ce = dyn_cast<ConstantExpr>(r)) {
			uint64_t	shift_v = ce->getZExtValue();

			if (shift_v < w)
				return SExtExpr::create(
					ExtractExpr::create(
						l->getKid(0),
						shift_v+ee->offset,
						w - shift_v),
					l->getWidth());
		}
	}

	return AShrExpr::alloc(l, r);
}

#define BCREATE_R(_e_op, _op, partialL, partialR) \
ref<Expr> OptBuilder::_op(const ref<Expr> &l, const ref<Expr> &r) {	\
	assert(l->getWidth()==r->getWidth() && "type mismatch");	\
	if (ConstantExpr *cl = dyn_cast<ConstantExpr>(l)) {		\
		if (ConstantExpr *cr = dyn_cast<ConstantExpr>(r))	\
			return cl->_op(cr);				\
		return _e_op ## _createPartialR(cl, r.get());		\
	} 								\
	if (ConstantExpr *cr = dyn_cast<ConstantExpr>(r)) {		\
		return _e_op ## _createPartial(l.get(), cr);		\
	}								\
	return _e_op ## _create(l.get(), r.get());			\
}

#define BCREATE(_e_op, _op) \
ref<Expr>  OptBuilder::_op(const ref<Expr> &l, const ref<Expr> &r) { \
	assert(l->getWidth()==r->getWidth() && "type mismatch");          \
	if (ConstantExpr *cl = dyn_cast<ConstantExpr>(l))                 \
		if (ConstantExpr *cr = dyn_cast<ConstantExpr>(r))         \
			return cl->_op(cr);                               \
	return _e_op ## _create(l, r);                                    \
}

BCREATE_R(AddExpr, Add, AddExpr_createPartial, AddExpr_createPartialR)
BCREATE_R(SubExpr, Sub, SubExpr_createPartial, SubExpr_createPartialR)
BCREATE_R(MulExpr, Mul, MulExpr_createPartial, MulExpr_createPartialR)
BCREATE_R(AndExpr, And, AndExpr_createPartial, AndExpr_createPartialR)
BCREATE_R(OrExpr, Or, OrExpr_createPartial, OrExpr_createPartialR)
BCREATE_R(XorExpr, Xor, XorExpr_createPartial, XorExpr_createPartialR)
BCREATE(UDivExpr, UDiv)
BCREATE(SDivExpr, SDiv)
BCREATE(URemExpr, URem)
BCREATE(SRemExpr, SRem)
BCREATE(ShlExpr, Shl)
BCREATE(LShrExpr, LShr)
BCREATE(AShrExpr, AShr)

static ref<Expr> EqExpr_create(const ref<Expr> &l, const ref<Expr> &r)
{
	const ConcatExpr	*ce_l, *ce_r;

	if (l == r)
		return ConstantExpr::alloc(1, Expr::Bool);

	ce_l = dyn_cast<ConcatExpr>(l);
	ce_r = dyn_cast<ConcatExpr>(r);
	if (ce_l && ce_r) {
		if (ce_l->getKid(0) == ce_r->getKid(0))
			return EqExpr::create(
				ce_l->getKid(1), ce_r->getKid(1));
		if (ce_l->getKid(1) == ce_r->getKid(1))
			return EqExpr::create(
				ce_l->getKid(0), ce_r->getKid(0));

		if (ce_l->getKid(0)->getWidth() == ce_r->getKid(0)->getWidth()
			&& ce_l->getWidth() <= 64)
		{
			return AndExpr::create(
				EqExpr::create(
					ce_l->getKid(0), ce_r->getKid(0)),
				EqExpr::create(
					ce_l->getKid(1), ce_r->getKid(1)));
		}
	}

	return EqExpr::alloc(l, r);
}

/***/
/// Tries to optimize EqExpr cl == rd, where cl is a ConstantExpr and
/// rd a ReadExpr.  If rd is a read into an all-constant array,
/// returns a disjunction of equalities on the index.  Otherwise,
/// returns the initial equality expression.
//
// XXX When does this "optimization" ever make sense? It's
// bounded by 100, but that still can cause some bad blow-up if it's
// being done all the time.
//
static ref<Expr> TryConstArrayOpt(
	const ref<ConstantExpr> &cl,
	ReadExpr *rd)
{
	if (rd->updates.getRoot()->isSymbolicArray() || rd->updates.getSize())
		return EqExpr_create(cl, rd);

	// Number of positions in the array that contain value ct.
	unsigned numMatches = 0;

	// for now, just assume standard "flushing" of a concrete array,
	// where the concrete array has one update for each index, in order
	ref<Expr> res = ConstantExpr::alloc(0, Expr::Bool);
	for (unsigned i = 0, e = rd->updates.getRoot()->mallocKey.size; i != e; ++i){
		if (cl != rd->updates.getRoot()->getValue(i))
			continue;

		// Arbitrary maximum on the size of disjunction.
		if (++numMatches > 100)
			return EqExpr_create(cl, rd);

		ref<Expr> mayBe = EqExpr::create(
			rd->index,
			ConstantExpr::alloc(i, rd->index->getWidth()));

		res = OrExpr::create(res, mayBe);
	}

	return res;
}

static ref<Expr> EqExpr_createPartialL(const ref<ConstantExpr> &cl, Expr *r)
{
	Expr::Width	width = cl->getWidth();
	Expr::Kind	rk = r->getKind();

	assert (r->getWidth() == cl->getWidth());

	if (width == Expr::Bool) {
		if (cl->isTrue())
			return r;

		// 0 == ...
		switch (rk) {
		case Expr::Eq: {
			const EqExpr *ree = cast<EqExpr>(r);
			ConstantExpr *CE = dyn_cast<ConstantExpr>(ree->left);

			// eliminate double negation
			// 0 == (0 == A) => A
			if (CE && CE->getWidth() == Expr::Bool && CE->isFalse())
				return ree->right;
			break;
		}
		case Expr::Or: {
			const OrExpr *roe = cast<OrExpr>(r);

			// transform not(or(a,b)) to and(not a, not b)
			return AndExpr::create(
				Expr::createIsZero(roe->left),
				Expr::createIsZero(roe->right));
		}
		case Expr::Ule:
			// !(l <= r) => l > r =>
			return UgtExpr::create(r->getKid(0), r->getKid(1));
		case Expr::Ult:
			// !(l < r) => l >= r =>
			return UgeExpr::create(r->getKid(0), r->getKid(1));
		case Expr::Sle:
			// !(l <= r) => l > r =>
			return SgtExpr::create(r->getKid(0), r->getKid(1));
		case Expr::Slt:
			// !(l < r) => l >= r =>
			return SgeExpr::create(r->getKid(0), r->getKid(1));
		default:
			break;
		}
	}

	switch (rk) {
	case Expr::SExt: {
		// (sext(a,T)==c) == (a==c)
		const SExtExpr *see = cast<SExtExpr>(r);
		Expr::Width fromBits = see->src->getWidth();
		ref<ConstantExpr> trunc = cl->ZExt(fromBits);

		// pathological check, make sure it is possible to
		// sext to this value *from any value*
		if (cl == trunc->SExt(width))
			return EqExpr::create(see->src, trunc);

		return ConstantExpr::create(0, Expr::Bool);
	}

	case Expr::ZExt: {
		// (zext(a,T)==c) == (a==c)
		const ZExtExpr *zee = cast<ZExtExpr>(r);
		Expr::Width fromBits = zee->src->getWidth();
		ref<ConstantExpr> trunc = cl->ZExt(fromBits);

		// pathological check, make sure it is possible to
		// zext to this value *from any value*
		if (cl == trunc->ZExt(width))
			return EqExpr::create(zee->src, trunc);

		return ConstantExpr::create(0, Expr::Bool);
	}

	case Expr::Add: {
		const AddExpr *ae = cast<AddExpr>(r);
		if (isa<ConstantExpr>(ae->left)) {
			// c0 = c1 + b => c0 - c1 = b
			return EqExpr_createPartialL(
				cast<ConstantExpr>(MK_SUB(cl, ae->left)),
				ae->right.get());
		}
	}
	break;

	case Expr::And: {
		const AndExpr 		*ae = cast<AndExpr>(r);
		const ConstantExpr	*ae_l;
		uint64_t		v;
		int			bit;

		if (!cl->isZero())
			break;

		if (ae->getWidth() > 64)
			break;

		ae_l = dyn_cast<ConstantExpr>(ae->left);
		if (ae_l == NULL)
			break;

		v = ae_l->getZExtValue();
		if (v == 0)
			break;

		bit = exact_log2(v);
		if (bit == -1)
			break;

		return EqExpr::create(
			cl,
			ZExtExpr::create(
				ExtractExpr::create(ae->right, bit, 1),
				width));
	}
	break;

	case Expr::Sub: {
		const SubExpr *se = cast<SubExpr>(r);
		if (isa<ConstantExpr>(se->left)) {
			// c0 = c1 - b => c1 - c0 = b
			return EqExpr_createPartialL(
				cast<ConstantExpr>(MK_SUB(se->left, cl)),
				se->right.get());
		}
	}
	break;

	case Expr::Read:
		if (ConstArrayOpt)
			return TryConstArrayOpt(cl, static_cast<ReadExpr*>(r));
	break;

	case Expr::Or: {
		/* constant masking rewrite */
		const OrExpr *oe = cast<OrExpr>(r);
		// (A == x | B) ==> (A & B == B)
		if (ConstantExpr *mask = dyn_cast<ConstantExpr>(oe->right)) {
			if (mask->And(cl) != mask)
				return ConstantExpr::alloc(0, Expr::Bool);
		}
	}
	break;


	case Expr::Concat: {
		ConcatExpr *ce = cast<ConcatExpr>(r);

		if (ce->getWidth() <= 64)
			break;

		return AndExpr::create(
			EqExpr_createPartialL(
				cl->Extract(
					ce->getRight()->getWidth(),
					ce->getLeft()->getWidth()),
				ce->getLeft().get()),
			EqExpr_createPartialL(
				cl->Extract(0, ce->getRight()->getWidth()),
				ce->getRight().get()));
	}


	case Expr::Select: {
		SelectExpr	*se = cast<SelectExpr>(r);
		if (	se->getKid(1)->getKind() == Expr::Constant &&
			se->getKid(2)->getKind() == Expr::Constant)
		{
			if (se->getKid(1) == cl)
				return se->getKid(0);
			if (se->getKid(2) == cl)
				return MK_NE(
					se->getKid(0),
					ConstantExpr::alloc(
						1, se->getKid(0)->getWidth()));
		}
	}
	break;

	default:
		break;
	}

	return EqExpr_create(cl, r);
}

static ref<Expr> EqExpr_createPartialR(Expr *l, const ref<ConstantExpr> &cr)
{ return EqExpr_createPartialL(cr, l); }

static ref<Expr> UltExpr_createPartialL(const ref<ConstantExpr> &c_l, Expr *r);
static ref<Expr> UltExpr_createPartialR(Expr *l, const ref<ConstantExpr> &cr);
static ref<Expr> UltExpr_create(const ref<Expr> &l, const ref<Expr> &r);

static ref<Expr> UltExpr_createPartialL(const ref<ConstantExpr> &c_l, Expr *r)
{
	/* (MAX < r) => never */
	if (c_l->isAllOnes())
		return MK_CONST(0, Expr::Bool);

	/* (0 < r) == (r != 0) */
	if (c_l->isZero())
		return MK_NE(c_l, r);

	return UltExpr_create(c_l, r);
}

static ref<Expr> UltExpr_createPartialR(Expr *l, const ref<ConstantExpr> &cr)
{
	/* l < 0-- never! => always return false */
	if (cr->isZero()) return MK_CONST(0, Expr::Bool);

	/* (l < MAX) == (l != MAX) */
	if (cr->isAllOnes())
		return MK_NE(cr, l);

	return UltExpr_create(l, cr);
}

static ref<Expr> UltExpr_create(const ref<Expr> &l, const ref<Expr> &r)
{
	Expr::Width t = l->getWidth();

	if (t == Expr::Bool)
		return AndExpr::create(Expr::createIsZero(l), r);

	if (l == r)
		return ConstantExpr::alloc(0, Expr::Bool);

	if (	l->getKind() == Expr::ZExt &&
		r->getKind() == Expr::Constant)
	{
		ZExtExpr	*ze = cast<ZExtExpr>(l);
		ConstantExpr	*ce = cast<ConstantExpr>(r);

		if (ce->getWidth() <= 64) {
			unsigned active_bits;
			active_bits = ze->src->getWidth();
			if ((1ULL << active_bits) <= ce->getZExtValue()) {
				// maximum value of lhs always less than rhs
				return ConstantExpr::alloc(1, Expr::Bool);
			}
		}
	}

/* Invalid optimization:  x + c0 < x + c1 => x < c0 - c1 */
#if 0
	if (	l->getKind() == Expr::Add &&
		r->getKind() == Expr::Constant)
	{
#error WHAT ARE YOU DOING STOP
		if (l->getKid(0)->getKind() == Expr::Constant) {
			return UltExpr::create(
				l->getKid(1),
				SubExpr::create(
					r,
					l->getKid(0)));
		}
	}
#endif

	return UltExpr::alloc(l, r);
}

static ref<Expr> UleExpr_create(const ref<Expr> &l, const ref<Expr> &r)
{
	// !(l && !r)
	if (l->getWidth() == Expr::Bool)
		return OrExpr::create(Expr::createIsZero(l), r);
	if (l->isZero())
		return ConstantExpr::alloc(1, Expr::Bool);
	if (r->isZero() || l == r)
		return EqExpr::create(l, r);

	if (r->getKind() == Expr::Constant) {
		ConstantExpr	*ce = cast<ConstantExpr>(r);

		/* (l <= ~0) is always true */
		if (ce->isAllOnes())
			return ConstantExpr::create(1, Expr::Bool);

		if (l->getKind() == Expr::ZExt)	{
			ZExtExpr	*ze = cast<ZExtExpr>(l);

			if (ce->getWidth() <= 64) {
				unsigned active_bits;
				active_bits = ze->src->getWidth();
				if ((1ULL << active_bits) < ce->getZExtValue()) {
					// maximum value of lhs always less than rhs
					return ConstantExpr::alloc(1, Expr::Bool);
				}
			}
		}
	}

	return UleExpr::alloc(l, r);
}

static ref<Expr> SltExpr_create(const ref<Expr> &l, const ref<Expr> &r)
{
	const ConstantExpr	*ce;

	// l && !r
	if (l->getWidth() == Expr::Bool)
		return MK_AND(l, Expr::createIsZero(r));

	/* is it a negativity check? l < r = 0 */
	if (	(ce = dyn_cast<ConstantExpr>(r)) &&
		ce->isZero())
	{
		/* two's complement trick-- negative <=> top bit set */
		return MK_EXTRACT(l, l->getWidth() - 1, 1);
	}

	return SltExpr::alloc(l, r);
}

static ref<Expr> SleExpr_create(const ref<Expr> &l, const ref<Expr> &r)
{
	// !(!l && r)
	if (l->getWidth() == Expr::Bool)
		return MK_OR(l, Expr::createIsZero(r));

	return SleExpr::alloc(l, r);
}

static ref<Expr> SleExpr_createPartialL(const ref<ConstantExpr> &c_l, Expr *r)
{
	// (<= -1 (zext n)) => (<= -1 non-neg)
	if (r->getKind() == Expr::ZExt) {
		ref<Expr>		extr(MK_EXTRACT(c_l, c_l->getWidth() - 1, 1));
		const ConstantExpr	*is_neg;
		is_neg = dyn_cast<ConstantExpr>(extr);
		assert (is_neg != NULL && "Extract of const did not yield const?");

		if (is_neg->isTrue()) {
			/* LHS is negative /\ RHS >= 0
			 * => RHS > LHS
			 * => LHS <= RHS == true */
			return MK_CONST(1, Expr::Bool);
		}

		// we know it's a comparison of two positives. Ule!
		return MK_ULE(c_l, r);
	}

	return SleExpr_create(c_l, r);
}

static ref<Expr> SleExpr_createPartialR(Expr *l, const ref<ConstantExpr> &cr)
{
	// (<= (zext n) -1) => (<= non-neg -1) => false
	if (l->getKind() == Expr::ZExt) {
		ref<Expr>		extr(MK_EXTRACT(cr, cr->getWidth() - 1, 1));
		const ConstantExpr	*is_neg;

		is_neg = dyn_cast<ConstantExpr>(extr);
		assert (is_neg != NULL && "Extract of const did not yield const?");

		if (is_neg->isTrue()) {
			/* RHS is negative /\ LHS >= 0
			 * => LHS > RHS
			 * => LHS <= RHS == false
			 */
			return MK_CONST(0, Expr::Bool);
		}

		// we know it's a comparison of two positives. Ule!
		return MK_ULE(l, cr);
	}

	return SleExpr_create(l, cr);
}



#define CMPCREATE(_e_op, _op) \
ref<Expr>  OptBuilder::_op(const ref<Expr> &l, const ref<Expr> &r) \
{ \
	assert(l->getWidth()==r->getWidth() && "type mismatch");              \
	if (ConstantExpr *cl = dyn_cast<ConstantExpr>(l))                     \
		if (ConstantExpr *cr = dyn_cast<ConstantExpr>(r)) {           \
			return cl->_op(cr);                                   \
		} \
	return _e_op ## _create(l, r);                                       \
}

#define CMPCREATE_T(_e_op, _op, _reflexive_e_op, partialL, partialR) \
ref<Expr>  OptBuilder::_op(const ref<Expr> &l, const ref<Expr> &r) {    \
  assert(l->getWidth()==r->getWidth() && "type mismatch");             \
  if (ConstantExpr *cl = dyn_cast<ConstantExpr>(l)) {                  \
    if (ConstantExpr *cr = dyn_cast<ConstantExpr>(r))                  \
      return cl->_op(cr);                                              \
    return partialL(cl, r.get());                                      \
  } else if (ConstantExpr *cr = dyn_cast<ConstantExpr>(r)) {           \
    return partialR(l.get(), cr);                                      \
  } else {                                                             \
    return _e_op ## _create(l.get(), r.get());                         \
  }                                                                    \
}


CMPCREATE_T(EqExpr, Eq, EqExpr, EqExpr_createPartialL, EqExpr_createPartialR)
CMPCREATE_T(SleExpr, Sle, SleExpr, SleExpr_createPartialL, SleExpr_createPartialR)
CMPCREATE_T(UltExpr, Ult, UltExpr, UltExpr_createPartialL, UltExpr_createPartialR)
CMPCREATE(UleExpr, Ule)
CMPCREATE(SltExpr, Slt)


void OptBuilder::printName(std::ostream& os) const
{ os << "OptBuilder\n"; }

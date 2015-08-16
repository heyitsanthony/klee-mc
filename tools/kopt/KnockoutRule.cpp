#include <iostream>

#include "../../lib/Expr/ExprRule.h"
#include "klee/Constraints.h"
#include "klee/Solver.h"
#include "static/Sugar.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/ExprTag.h"
#include "klee/util/Assignment.h"
#include "klee/util/Subtree.h"

#include "KnockOut.h"
#include "KnockoutRule.h"


using namespace klee;
KnockoutRule::KnockoutRule(const ExprRule* _er, ref<Array>& ko_arr)
: er(_er)
, arr(ko_arr)
{
	ref<Expr>	e;

	e = er->getFromExpr();
	kout = new KnockOut(arr);
	ko = kout->apply(e);
	if (kout->getArrOff() == 0)
		ko = NULL;
}

KnockoutRule::~KnockoutRule(void) { delete kout; }

#define RANGE_EXPR(target, lo, hi)	\
	MK_AND(MK_ULE(lo, target), MK_ULE(target, hi))

static bool isValidRange(
	Solver* s,
	const ref<Expr>& rule_eq,
	const ref<Expr>& e_range)
{
	ConstraintManager	cs;
	Query			q(cs, rule_eq);
	bool			mustBeTrue;

	/* if it's constant, it is always true and should be matched
	 * with a dummy var, not a constant label! */
	if (e_range->getKind() == Expr::Constant)
		return false;

	std::cerr << "RANGE EXPR: " << e_range << '\n';
	cs.addConstraint(e_range);
	if (s->mustBeTrue(q, mustBeTrue) == false)
		return false;

	if (mustBeTrue) {
		std::cerr << "================\n";
		q.print(std::cerr);
		std::cerr << "===========+++++++++++++======\n";
	}

	return mustBeTrue;
}


static ref<Expr> getContiguousRange(Solver* s, Query& q, uint64_t pivot)
{
	std::pair<uint64_t, uint64_t >		r;
	std::pair<ref<Expr>, ref<Expr> >	r_e;

	/* formula for query = value for slotted constant */
	if (!s->getImpliedRange(q, pivot, r)) {
		std::cerr << "FAILED RANGE\n";
		return NULL;
	}

	if (r.first == r.second) {
		std::cerr << "SINGLETON RANGE\n";
		return NULL;
	}

	r_e.first = MK_CONST(r.first, q.expr->getWidth());
	r_e.second = MK_CONST(r.second, q.expr->getWidth());

	return RANGE_EXPR(q.expr, r_e.first, r_e.second);
}

#if 0
static ref<Expr> getMaskRange(Solver* s, Query& q, uint64_t pivot)
{
	ref<Expr>	range;
	unsigned	w;
	uint64_t	care_bits;	/* bits that are valid  */

	w = q.expr->getWidth();
	if (w > 64) return NULL;

	if (w > 64) w = 64;

	/* TODO : this is probably so slow */
	care_bits = 0;
	for (unsigned i = 0; i < w; i++) {
		uint64_t		bit_mask = ((uint64_t)1) << i;
		bool			mbt = false;
		Query			q2(
			q.constraints,
			MK_EQ(	MK_AND(q.expr, MK_CONST(bit_mask, w)),
				MK_CONST(pivot & bit_mask, w)));

		if (!s->mustBeTrue(q2, mbt))
			return NULL;
		if (mbt) care_bits |= bit_mask;
	}

	/* exact match? wtf?? */
	if (care_bits == (uint64_t)((1LL << w)-1)) {
		std::cerr << "Exact match on MaskRange?!\n";
		return NULL;
	}

	if (care_bits == 0)
		return NULL;

	range = MK_EQ(
		MK_AND(MK_CONST(care_bits, w), q.expr),
		MK_CONST(pivot & care_bits, w));

	assert (range->getKind() != Expr::Constant);
	return range;
}
#endif

static bool isConstFixed(Solver* s, Query& q, const ref<Expr>& v)
{
	bool	mbt;
	Query	q2(q.constraints, MK_EQ(q.expr, v));
	if (s->mustBeTrue(q2, mbt) == false) return false;
	return mbt;
}

ref<Expr> KnockoutRule::trySlot(
	Solver* s, const ref<Expr>& e_from,
	const replvar_t& rv, int i) const
{
	KnockOut			kout_partial(arr, i);
	ref<Expr>			rule_eq, e_range, e_ko;
	uint64_t			pivot;
	ConstraintManager		cs;
	Query				q(cs, rv.first);

	/* knock out constant we intend to slot out */
	e_ko = kout_partial.apply(e_from);
	pivot = rv.second->getZExtValue();

	/* e_ko is a superset of e_to because of removed constant;
	 * constrain */
	rule_eq = MK_EQ(e_ko, er->getToExpr());
	cs.addConstraint(rule_eq);

	/* constant can't be changed? shoot. */
	if (isConstFixed(s, q, rv.second)) {
		std::cerr << "[KO] CONST IS FIXED\n";
		return NULL;
	}

#if 0
	/* XXX: THIS DOESN'T WORK BECAUSE THE CONSTANT LABELING
	 * DOESN'T KNOW ABOUT THE OFFSETS IN THE CONSTANTS?? UGH! */
	e_range = getMaskRange(s, q, pivot);
	if (e_range.isNull() == false) {
		if (isValidRange(s, rule_eq, e_range)) {
			std::cerr << "GOT MASK RANGE: " << e_range << '\n';
			goto done;
		}
		std::cerr
			<< "INVALID RANGE [" << i << "]: "
			<< e_range << '\n';
	}
#else
	std::cerr << "MASK RANGE DISABLED BECAUSE BUSTED\n";
#endif
	e_range = getContiguousRange(s, q, pivot);
	if (e_range.isNull()) return NULL;
	if (isValidRange(s, rule_eq, e_range)) goto done;

	std::cerr
		<< "INVALID RANGE [" << i << "]: "
		<< e_range << '\n';
	return NULL;
done:
	er->print(std::cerr);
#if 0
	std::cerr << "\n=========GOT SLOT=============\n";
	std::cerr << "\nIT-FIRST: " << rv.first << '\n';
	std::cerr << "IGN-IDX: " << i << '\n';
	std::cerr << "E-FROM: " << e_from << '\n';
	std::cerr << "PART-KO: " << e_ko << '\n';
	std::cerr << "ORIG-KO: " << ko << '\n';
	std::cerr << "RANGE-EXPR " << e_range << '\n';
	std::cerr << "\n=====================\n";
#endif
	return e_range;
}

/* start with the original expression;
 * successively knock out constants */
bool KnockoutRule::findRuleRange(
	Solver* s, int& slot, ref<Expr>& e_range) const
{
	ref<Expr>	e_from;
	int		i;

	e_from = er->getFromExpr();
	i = 0;

	std::cerr << "TRYING RANGES FOR:\n";
	std::cerr << "FROM-EXPR={" << e_from << "}\n";
	std::cerr << "TO-EXPR={" << er->getToExpr() << "}\n";

	foreach (it, kout->begin(), kout->end()) {
		e_range = trySlot(s, e_from, *it, i);

		if (!e_range.isNull()) {
			slot = i;
			return true;
		}
		i++;
	}

	return false;
}

ref<Expr> KnockoutRule::getKOExpr(void) const
{ return (ko.isNull()) ? er->getFromExpr() : ko; }

bool KnockoutRule::isConstInvariant(Solver* s) const
{
	ref<Expr>	to_expr;
	bool		ok, mustBeTrue;

	to_expr = er->getToExpr();

	ok = s->mustBeTrue(
		Query(EqExpr::create(ko, to_expr)),
		mustBeTrue);
	if (ok == false) {
		std::cerr << "QUERY FAILED: KO="
			<< ko << "\nTO=" << to_expr << '\n';
		return false;
	}

	return mustBeTrue;
}

exprtags_ty KnockoutRule::getTags(int slot) const
{
	ExprVisitorTagger<KnockOut>	tag_kout;
	ref<Expr>			e_from;

	tag_kout.setParams(arr, slot);
	e_from = er->getFromExpr();
	tag_kout.apply(e_from);

	assert (tag_kout.getPreTags().size() > 0);
	return tag_kout.getPreTags();
}

ExprRule* KnockoutRule::createFullRule(Solver* s) const
{
	ExprRule			*er_ret;
	exprtags_ty			tags;

	if (er->hasConstraints())
		return NULL;

	if (!isConstInvariant(s))
		return NULL;

	tags = getTags();

	std::vector<ref<Expr> >	cs(tags.size(), MK_CONST(1, 1));

	er_ret = er->addConstraints(tags, cs);
	return er_ret;
}

ExprRule* KnockoutRule::createPartialRule(Solver* s) const
{
	ExprRule			*er_ret;
	exprtags_ty			tags;
	int				slot;
	ref<Expr>			e_range;

	if (!findRuleRange(s, slot, e_range))
		return NULL;

	tags = getTags(slot);
	if (tags.size() != 1)
		std::cerr << "WHOOPS! SLOT="
			<< slot
			<< ". PRETAGS = "
			<< tags.size() << '\n';
	assert (tags.size() == 1);

	std::vector<ref<Expr> >	cs(tags.size(), e_range);

	er_ret = er->addConstraints(tags, cs);
	return er_ret;
}

ExprRule* KnockoutRule::createSubtreeRule(Solver *s) const
{
	std::pair<ref<Expr>, ref<Expr> >	split_e;
	ref<Expr>	from_e;
	unsigned	i, tag;

	from_e = er->getFromExpr();
	split_e.first = from_e;
	/* check knock out each subtree, check for validity */
	for (i = 0; !split_e.first.isNull(); i++) {
		ref<Expr>	cond;
		bool		mbt;

		split_e = Subtree::getSubtree(arr, from_e, i);
		if (split_e.first.isNull())
			break;

		if (split_e.first == from_e)
			continue;

		/* we've already marked this one as being a free
		 * subtree-- don't redo it. */
		if (split_e.second->getKind() == Expr::NotOptimized)
			continue;

		/* note: we still traverse the notopt subtree,
		 * so this detects its format so we don't touch it */
		if (split_e.second->getKind() == Expr::Concat) {
			ref<Expr>	kid(split_e.second->getKid(0));

			if (kid->getKind() == Expr::Read) {
				if (cast<ReadExpr>(kid)->getArray() ==
					Pattern::getFreeArray())
				{
					continue;
				}
			}
		}

		/* ignore reads-- they've already got symbolics */
		if (split_e.second->getKind() == Expr::Read)
			continue;

		/* is from-expr necessarily equal to subtree expr? */
		cond = EqExpr::create(split_e.first, from_e);
		if (!s->mustBeTrue(Query(cond), mbt))
			continue;

		/* yes, the two are equivalent. get chopping */
		if (mbt == true)
			break;
	}

	if (split_e.first.isNull())
		return NULL;

	assert (from_e != split_e.first);

	/* get tag of what to remove */
	tag = Subtree::getTag(arr, from_e, i);
	return er->markUnbound(tag);
}

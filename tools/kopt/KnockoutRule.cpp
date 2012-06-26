#include <iostream>

#include "../../lib/Expr/ExprRule.h"
#include "klee/Constraints.h"
#include "klee/Solver.h"
#include "static/Sugar.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/ExprTag.h"
#include "klee/util/Assignment.h"
#include "klee/util/Subtree.h"
#include <llvm/Support/CommandLine.h>

#include "KnockoutRule.h"


using namespace klee;
using namespace llvm;

namespace 
{
	cl::opt<unsigned>
	KOConsts("ko-consts", cl::desc("KO const widths"), cl::init(8));
}

class klee::KnockOut : public ExprVisitor
{
public:
	KnockOut(ref<Array> _arr = NULL, int _uri = -1)
	: ExprVisitor(false, true)
	, arr(_arr)
	, uniqReplIdx(_uri)
	{ use_hashcons = false; }
	virtual ~KnockOut() {}

	void setParams(ref<Array>& _arr, int _uri = -1)
	{ arr = _arr; uniqReplIdx = _uri; }

	ref<Expr> apply(const ref<Expr>& e)
	{
		arr_off = 0;
		replvars.clear();
		return visit(e);
	}

	// NOTE: this mean that tag users must also skip reads
	virtual Action visitRead(const ReadExpr& re)
	{ return Action::skipChildren(); }

	virtual Action visitConstant(const ConstantExpr& ce)
	{
		ref<Expr>	new_expr;

		if (isIgnoreConst(ce))
			return Action::skipChildren();

		new_expr = Expr::createTempRead(arr, ce.getWidth(), arr_off);

		replvars.push_back(
			replvar_t(
				new_expr,
				const_cast<ConstantExpr*>(&ce)));
		arr_off += ce.getWidth() / 8;

		/* unconditional change-to */
		if (uniqReplIdx == -1)
			return Action::changeTo(new_expr);

		/* only accept *one* replacement into new expression */
		if (uniqReplIdx == ((int)replvars.size()-1))
			return Action::changeTo(new_expr);

		return Action::skipChildren();
	}

	unsigned getArrOff(void) const { return arr_off; }

	std::vector<replvar_t>::const_iterator begin(void) const
	{ return replvars.begin(); }

	std::vector<replvar_t>::const_iterator end(void) const
	{ return replvars.end(); }
	unsigned getNumReplVars(void) const { return replvars.size(); }
private:
	unsigned	arr_off;
	ref<Array>	arr;
	int		uniqReplIdx;

	bool isIgnoreConst(const ConstantExpr& ce)
	{
		switch (KOConsts) {
		case 64:
			return (ce.getWidth() != 64);
		case 32:
			return (ce.getWidth() != 64 &&
				ce.getWidth() != 32);
		case 16:
			return (ce.getWidth() != 64 &&
				ce.getWidth() != 32 &&
				ce.getWidth() != 16);
		case 8:
			return (ce.getWidth() != 64 &&
				ce.getWidth() != 32 &&
				ce.getWidth() != 16 &&
				ce.getWidth() != 8);
		case 1:
			return (ce.getWidth() % 8 != 0 || ce.getWidth() > 64);
		default:
			assert (0 == 1);
		}
		return true;
	}

	std::vector<replvar_t>	replvars;
};

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

static ref<Expr> getRangeExpr(
	const ref<Expr>& target,
	const ref<Expr>& lo,
	const ref<Expr>& hi)
{
	return AndExpr::create(
		UleExpr::create(lo, target),
		UleExpr::create(target, hi));
}

static bool isValidRange(
	Solver* s,
	const ref<Expr>& rule_eq,
	const ref<Expr>& target,
	const ref<Expr>& lo,
	const ref<Expr>& hi)
{
	ConstraintManager	cs;
	Query			q(cs, rule_eq);
	bool			mustBeTrue;

	cs.addConstraint(getRangeExpr(target, lo, hi));
	if (s->mustBeTrue(q, mustBeTrue) == false)
		return false;

	if (mustBeTrue) {
		std::cerr << "================\n";
		q.print(std::cerr);
		std::cerr << "===========+++++++++++++======\n";
	}

	return mustBeTrue;
}


ref<Expr> KnockoutRule::trySlot(
	Solver* s, const ref<Expr>& e_from,
	const replvar_t& rv, int i) const
{
	KnockOut		kout_partial(arr, i);
	ref<Expr>		rule_eq, e_range, e_ko;
	std::pair< ref<Expr>, ref<Expr> >	r;
	ConstraintManager	cs;
	Query			q(cs, rv.first);

	e_ko = kout_partial.apply(e_from);
	rule_eq = EqExpr::create(e_ko, er->getToExpr());
	cs.addConstraint(rule_eq);

	if (!s->getRange(q, r)) {
		std::cerr << "FAILED RANGE\n";
		return NULL;
	}
	if (r.first == r.second) {
		std::cerr << "SINGLETON RANGE\n";
		return NULL;
	}
	if (!isValidRange(s, rule_eq, rv.first, r.first, r.second)) {
		std::cerr << "INVALID RANGE [" << i << "]: ["
			<< r.first << ", " << r.second << "]\n";
		return NULL;
	}

	e_range = getRangeExpr(rv.first, r.first, r.second);

	er->print(std::cerr);
	std::cerr << "\n======================\n";
	std::cerr << "\nIT-FIRST: " << rv.first << '\n';
	std::cerr << "IGN-IDX: " << i << '\n';
	std::cerr << "E-FROM: " << e_from << '\n';
	std::cerr << "PART-KO: " << e_ko << '\n';
	std::cerr << "ORIG-KO: " << ko << '\n';
	std::cerr << "RANGE-EXPR " << e_range << '\n';
	std::cerr << "\n=====================\n";

	return e_range;
}

/* start with the original expression;
 * successively knock out constants */
bool KnockoutRule::isRangedRuleValid(
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

	std::vector<ref<Expr> >	cs(tags.size(), ConstantExpr::create(1, 1));

	er_ret = er->addConstraints(tags, cs);
	return er_ret;
}

ExprRule* KnockoutRule::createPartialRule(Solver* s) const
{
	ExprRule			*er_ret;
	exprtags_ty			tags;
	int				slot;
	ref<Expr>			e_range;

	if (!isRangedRuleValid(s, slot, e_range))
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
		/* note, we'll still traverse the notopt subtree, but
		 * we already know its format */
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

#include <iostream>

#include "../../lib/Expr/ExprRule.h"
#include "klee/Constraints.h"
#include "klee/Solver.h"
#include "static/Sugar.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/ExprTag.h"
#include "klee/util/Assignment.h"
#include <llvm/Support/CommandLine.h>

#include "KnockoutRule.h"


using namespace klee;
using namespace llvm;

namespace 
{
	cl::opt<unsigned>
	KOConsts("ko-consts", cl::desc("KO const widths"), cl::init(32));
}

class ConstCounter : public ExprConstVisitor
{
public:
	typedef	std::map<ref<ConstantExpr>, unsigned> constcount_ty;

	ConstCounter(void) : ExprConstVisitor(false) {}
	virtual Action visitExpr(const Expr* expr)
	{
		ref<ConstantExpr>	r_ce;

		if (expr->getKind() != Expr::Constant)
			return Expand;

		r_ce = ref<ConstantExpr>(
			static_cast<ConstantExpr*>(
				const_cast<Expr*>(expr)));
		const_c[r_ce] = const_c[r_ce] + 1;
		return Expand;
	}

	constcount_ty::const_iterator begin(void) const
	{ return const_c.begin(); }

	constcount_ty::const_iterator end(void) const
	{ return const_c.end(); }
private:
	constcount_ty	const_c;
};

class klee::KnockOut : public ExprVisitor
{
public:
	// first = symbolic expr, second = old constant
	typedef std::pair<ref<Expr>, ref<ConstantExpr> >	replvar_t;

	KnockOut(const Array* _arr = NULL, int _uri = -1)
	: ExprVisitor(false, true)
	, arr(_arr)
	, uniqReplIdx(_uri)
	{ use_hashcons = false; }
	virtual ~KnockOut() {}

	void setParams(const Array* _arr, int _uri = -1)
	{ arr = _arr; uniqReplIdx = _uri; }

	ref<Expr> apply(const ref<Expr>& e)
	{
		arr_off = 0;
		replvars.clear();
		return visit(e);
	}

	virtual Action visitRead(const ReadExpr& re)
	{ return Action::skipChildren(); }

	virtual Action visitConstant(const ConstantExpr& ce)
	{
		ref<Expr>	new_expr;

		if (isIgnoreConst(ce))
			return Action::skipChildren();

		new_expr = Expr::createTempRead(
			ARR2REF(arr), ce.getWidth(), arr_off);

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
	const Array	*arr;
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

KnockoutRule::KnockoutRule(const ExprRule* _er, const Array* ko_arr)
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

/* start with the original expression;
 * successively knock out constants */
bool KnockoutRule::isRangedRuleValid(
	Solver* s, int& slot, ref<Expr>& e_range) const
{
	ref<Expr>	e_from;
	int		i;


	e_from = er->getFromExpr();
	i = -1;

	std::cerr << "TRYING RANGES FOR:\n";
	std::cerr << "FROM-EXPR={" << e_from << "}\n";
	std::cerr << "TO-EXPR={" << er->getToExpr() << "}\n";
	foreach (it, kout->begin(), kout->end()) {
		KnockOut		kout_partial(arr, i+1);
		ref<Expr>		rule_eq;
		std::pair< ref<Expr>, ref<Expr> >	r;
		ref<Expr>		e_ko;
		ConstraintManager	cs;
		Query			q(cs, it->first);

		i++;

		e_ko = kout_partial.apply(e_from);
		rule_eq = EqExpr::create(e_ko, er->getToExpr());
		cs.addConstraint(rule_eq);

		if (!s->getRange(q, r)) {
			std::cerr << "FAILED RANGE\n";
			continue;
		}
		if (r.first == r.second) {
			std::cerr << "SINGLETON RANGE\n";
			continue;
		}
		if (!isValidRange(s, rule_eq, it->first, r.first, r.second)) {
			std::cerr << "INVALID RANGE: ["
				<< r.first << ", " << r.second << "]\n";
			continue;
		}

		e_range = getRangeExpr(it->first, r.first, r.second);

		er->printPrettyRule(std::cerr);
		std::cerr << "\n======================\n";
		std::cerr << "\nIT-FIRST: " << it->first << '\n';
		std::cerr << "IGN-IDX: " << i << '\n';
		std::cerr << "E-FROM: " << e_from << '\n';
		std::cerr << "PART-KO: " << e_ko << '\n';
		std::cerr << "ORIG-KO: " << ko << '\n';
		std::cerr << "RANGE-EXPR " << e_range << '\n';
		std::cerr << "\n=====================\n";
		slot = i;
		return true;
	}

	return false;
}

ref<Expr> KnockoutRule::getKOExpr(void) const
{
	return (ko.isNull()) ? er->getFromExpr() : ko;
}

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

	if (!isConstInvariant(s))
		return NULL;

	tags = getTags();

	std::vector<ref<Expr> >	cs(tags.size(), ConstantExpr::create(1, 1));

	er_ret = er->addConstraints(arr, tags, cs);
	assert (er_ret != NULL);
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

	er_ret = er->addConstraints(arr, tags, cs);
	assert (er_ret != NULL);
	return er_ret;
}

/* create a knocked-out rule */
ExprRule* KnockoutRule::createRule(Solver* s) const
{
	ExprRule	*ret;

	/* nothing knocked out => can't make a new rule */
	if (!knockedOut())
		return NULL;

	if ((ret = createFullRule(s)) != NULL)
		return ret;

	if ((ret = createPartialRule(s)) != NULL)
		return ret;

	return NULL;
}

#if 0
ConstCounter	cc;
cc.visit(from_expr);
std::cerr << "FROM-EXPR: " << from_expr << '\n';
foreach (it2, cc.begin(), cc.end()) {
	ref<Expr>		is_ugt_bound;
	ref<ConstantExpr>	e;

	std::cerr
		<< it2->first << "[" << it2->first->getWidth()
		<< "]: " << it2->second << '\n';
}
#endif

#include <iostream>

#include "../../lib/Expr/ExprRule.h"
#include "../../lib/Expr/RuleBuilder.h"
#include "klee/Expr.h"
#include "klee/ExprBuilder.h"
#include "klee/Solver.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/Assignment.h"
#include "static/Sugar.h"

#include "DBScan.h"

using namespace klee;

extern ExprBuilder* createExprBuilder(void);

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

class KnockOut : public ExprVisitor
{
public:
	KnockOut(const Array* _arr)
	: ExprVisitor(false, true)
	, arr(_arr)
	{ use_hashcons = false; }
	virtual ~KnockOut() {}

	ref<Expr> knockoutConsts(ref<Expr>& e)
	{
		arr_off = 0;
		return visit(e);
	}


	virtual Action visitRead(const ReadExpr& re)
	{ return Action::skipChildren(); }

	virtual Action visitConstant(const ConstantExpr& ce)
	{
		ref<Expr>	new_expr;

		if (!(ce.getWidth() == 32 || ce.getWidth() == 64))
			return Action::skipChildren();

		new_expr = Expr::createTempRead(
			ARR2REF(arr), ce.getWidth(), arr_off);
		arr_off += ce.getWidth() / 8;
		return Action::changeTo(new_expr);
	}

private:
	unsigned	arr_off;
	const Array	*arr;
};

bool DBScan::queryKnockout(const ExprRule* er, Solver* s)
{
	KnockOut	ko(arr.get());
	ref<Expr>	ko_expr, to_expr;
	bool		ok, mustBeTrue;

	ko_expr = er->getFromExpr();
	ko_expr = ko.knockoutConsts(ko_expr);
	to_expr = er->getToExpr();

	ok = s->mustBeTrue(
		Query(EqExpr::create(ko_expr, to_expr)),
		mustBeTrue);

	if (ok == false) {
		std::cerr << "QUERY FAILED: KO="
			<< ko_expr << "\nTO="
			<< to_expr << '\n';
	}

	if (mustBeTrue == false)
		return false;

	return true;
}

extern ExprBuilder* createExprBuilder(void);


DBScan::DBScan(void)
{
	arr = Array::create("ko_arr", 4096);
	rb = new RuleBuilder(createExprBuilder());
}

DBScan::~DBScan() { delete rb; }


void DBScan::loadKnockouts(komap_ty& ko_map)
{
	KnockOut	ko(arr.get());

	foreach (it, rb->begin(), rb->end()) {
		const ExprRule*	er;
		ref<Expr>	from_expr, ko_expr;

		er = *it;
		from_expr = er->getFromExpr();
		ko_expr = ko.knockoutConsts(from_expr);

		/* nothing changed? */
		if (ko_expr == from_expr)
			continue;

		ko_map[ko_expr].push_back(er);
	}
}


void DBScan::punchout(Solver* s)
{
	komap_ty			ko_map;
	std::map<unsigned, unsigned>	ko_c;
	unsigned			match_c, rule_match_c;

	loadKnockouts(ko_map);

	rule_match_c = match_c = 0;
	foreach (it, ko_map.begin(), ko_map.end()) {
		const ExprRule	*er = it->second.front();
		unsigned	rule_c;

		rule_c = it->second.size();
		ko_c[rule_c] = ko_c[rule_c] + 1;

		/* don't try anything with unique rules */
		if (rule_c < 2)
			continue;

		if (rule_c > 16) {
			std::cerr << "MEGA-RULE: RULE_MATCH=" << rule_c
				<< "\nKO=" << it->first
				<< "\nEXAMPLE-FROM=" << er->getFromExpr()
				<< "\n\n";
		}



		if (queryKnockout(er, s) == false)
			continue;

		std::cerr
			<< "MATCH KO: " << it->first << "\nRuleCount="
			<< rule_c << '\n'
			<< "example-from: " << er->getFromExpr() << '\n';

		match_c++;
		rule_match_c += rule_c;
	}

	std::cerr << "TOTAL MATCHED KO's: " << match_c << '\n';
	std::cerr << "TOTAL RULES MATCHED: " << rule_match_c << '\n';

	foreach (it, ko_c.begin(), ko_c.end()) {
		std::cerr << "KO_C: RULES=[" << it->first << "] : "
			<< it->second << '\n';
	}
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

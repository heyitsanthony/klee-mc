#include <iostream>
#include "../../lib/Expr/ExprRule.h"
#include "../../lib/Expr/RuleBuilder.h"
#include "klee/Expr.h"
#include "klee/ExprBuilder.h"
#include "klee/Solver.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/Assignment.h"
#include "static/Sugar.h"

using namespace klee;

extern ExprBuilder::BuilderKind	BuilderKind;

bool checkRule(const ExprRule* er, Solver* s, std::ostream&);

static ref<Expr> getLabelErrorExpr(const ExprRule* er)
{
	/* could have been a label error;
	 * labels_to not a proper subset of labels_from */
	ref<Expr>			from_expr, to_expr;
	std::vector<ref<ReadExpr> >	from_reads, to_reads;
	std::set<ref<ReadExpr> >	from_set, to_set;

	to_expr = er->getToExpr();
	from_expr = er->getFromExpr();
	ExprUtil::findReads(from_expr, false, from_reads);
	ExprUtil::findReads(to_expr, false, to_reads);

	foreach (it, from_reads.begin(), from_reads.end())
		from_set.insert(*it);

	foreach (it, to_reads.begin(), to_reads.end())
		to_set.insert(*it);

	foreach (it, to_set.begin(), to_set.end()) {
		if (from_set.count(*it))
			continue;

		/* element in to-set that does not
		 * exist in from set?? Likely a constant. */
		Assignment		a(to_expr);
		ref<Expr>		v;
		const ConstantExpr	*ce;

		a.bindFreeToZero();
		v = a.evaluate(to_expr);
		ce = dyn_cast<ConstantExpr>(v);
		if (ce == NULL)
			break;

		/* fixup idea here is that it might actually be a constant
		 * rule; so evaluate with whatever params to get a const
		 * thentry that as the to-expr instead */
		// std::cerr << "Attempting LabelError fixup for"
		//	<< "\nfrom-expr=" << from_expr
		//	<< "\nto-expr=" << to_expr << '\n';
		// std::cerr << "=========\n";
		return v;
	}

	return NULL;
}

typedef std::list<
	std::pair<
		RuleBuilder::rulearr_ty::const_iterator,
		ref<Expr> > > rule_replace_ty;

typedef std::set<const ExprRule*>	bad_repl_ty;

static void findReplacements(
	ExprBuilder* eb,
	RuleBuilder* rb,
	rule_replace_ty& replacements)
{
	unsigned	i;

	i = 0;
	foreach (it, rb->begin(), rb->end()) {
		const ExprRule	*er = *it;
		ref<Expr>	old_to_expr, rb_to_expr;
		ExprBuilder	*old_eb;

		old_to_expr = er->getToExpr();
		old_eb = Expr::setBuilder(rb);
		rb_to_expr = er->getToExpr();
		Expr::setBuilder(old_eb);

		i++;

		/* no effective transitive rule? */
		if (old_to_expr == rb_to_expr) {
			rb_to_expr = getLabelErrorExpr(er);
			if (rb_to_expr.isNull())
				continue;
		}

		/* transitive rule does not reduce nodes? */
		if (	ExprUtil::getNumNodes(rb_to_expr) >=
			ExprUtil::getNumNodes(old_to_expr))
		{
			continue;
		}

		std::cerr << "Xtive [" << i << "]:\n";
		er->printPrettyRule(std::cout);
		std::cerr	<< "OLD-TO-EXPR: " << old_to_expr << '\n'
				<< "NEW-TO-EXPR: " << rb_to_expr << '\n';

		replacements.push_back(std::make_pair(it, rb_to_expr));
	}
}

void appendReplacements(
	RuleBuilder* rb,
	Solver* s,
	rule_replace_ty& repl,
	bad_repl_ty& bad_repl)
{
	std::ofstream	of(
		rb->getDBPath().c_str(),
		std::ios_base::out |
		std::ios_base::app |
		std::ios_base::binary);
	foreach (it, repl.begin(), repl.end()) {
		const ExprRule	*er = *(it->first);
		ExprRule	*xtive_er;

		xtive_er = ExprRule::changeDest(er, it->second);
		if (xtive_er == NULL)
			continue;

		xtive_er->printPrettyRule(std::cout);
		if (checkRule(xtive_er, s, std::cerr) == false) {
			bad_repl.insert(er);
			continue;
		}

		xtive_er->printBinaryRule(of);
	}
	of.close();
}

void xtiveBRule(ExprBuilder *eb, Solver* s)
{
	RuleBuilder		*rb;
	bad_repl_ty		bad_repl;
	rule_replace_ty		replacements;

	rb = new RuleBuilder(ExprBuilder::create(BuilderKind));
	findReplacements(eb, rb, replacements);

	// append new rules to brule file
	appendReplacements(rb, s, replacements, bad_repl);

	// erase all superseded rules (ignoring ones that weren't valid)
	foreach (it, replacements.begin(), replacements.end()) {
		const ExprRule	*er = *(it->first);
		if (bad_repl.count(er))
			continue;
		rb->eraseDBRule(it->first);
	}

	delete rb;
}

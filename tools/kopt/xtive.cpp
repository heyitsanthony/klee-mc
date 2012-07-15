#include <llvm/Support/CommandLine.h>
#include <iostream>
#include "../../lib/Expr/ExprRule.h"
#include "../../lib/Expr/RuleBuilder.h"
#include "../../lib/Expr/ExprReplaceVisitor.h"
#include "klee/Expr.h"
#include "klee/ExprBuilder.h"
#include "klee/Solver.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/Assignment.h"
#include "BuiltRule.h"
#include "static/Sugar.h"

using namespace klee;

namespace
{
	llvm::cl::opt<bool>
	NoisyXtive(
		"xtive-noisy",
		llvm::cl::desc("Print generated transivity rules"),
		llvm::cl::init(false));
}

extern ExprBuilder::BuilderKind	BuilderKind;

bool checkRule(const ExprRule* er, Solver* s, std::ostream&);
bool getRuleCex(const ExprRule* er, Solver* s, std::ostream&);
bool getExprCex(
	Solver* s, const ref<Expr>& e1, const ref<Expr>& e2,
	std::ostream& os);

static ref<Expr> fixupDisjointLabels(
	ref<Expr>& to_expr,
	const std::set<ref<ReadExpr> >& disjoined)
{
	/* Element in to-set that does not exist in from set??
	 * Must be nonsense. Replace with 0. */
	ref<Expr>	v(to_expr);

	foreach (it, disjoined.begin(), disjoined.end()) {
		ExprReplaceVisitor	erv(*it, ConstantExpr::create(0, 8));
		v = erv.apply(v);
	}

	return v;
}

static ref<Expr> getLabelErrorExpr(const ExprRule* er)
{
	/* could have been a label error;
	 * labels_to not a proper subset of labels_from */
	ref<Expr>			from_expr, to_expr;
	std::vector<ref<ReadExpr> >	from_reads, to_reads;
	std::set<ref<ReadExpr> >	from_set, to_set;
	std::set<ref<ReadExpr> >	disjoined;

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

		disjoined.insert(*it);

	}

	if (disjoined.empty())
		return NULL;

	return fixupDisjointLabels(to_expr, disjoined);
}

typedef std::list<
	std::pair<
		RuleBuilder::rulearr_ty::const_iterator,
		ref<Expr> > > rule_replace_ty;

typedef std::set<const ExprRule*>	bad_repl_ty;

static unsigned appendNewFroms(Solver* s, RuleBuilder* rb)
{
	ExprBuilder	*init_eb;
	unsigned	new_rule_c;
	std::ofstream	of(
		rb->getDBPath().c_str(),
		std::ios_base::out |
		std::ios_base::app |
		std::ios_base::binary);

	init_eb = Expr::getBuilder();
	new_rule_c = 0;
	foreach (it, rb->begin(), rb->end()) {
		const ExprRule	*er(*it);
		BuiltRule	br(init_eb, rb, er);
		ExprRule	*new_rule;

		/* translation of from->to worked perfectly? great! */
		if (br.builtAsExpected())
			continue;

		/* better than to-expr -- handled elsewhere */
		if (br.isBetter()) {
			std::cerr << ">>>>>>>>>IS BETTER\n";
			continue;
		}

		/* careful-- we want to be monotone decreasing or 
		 * the size of the db could explode */
		/* I think this is OK though, since sometimes the 
		 * hand optimizer will make things slightly larger.
		 * If the DB *does* explode, maybe revisit this. */
		if (br.isWorse()) {
			std::cerr << "WARNING: Making rule for worse expr.\n";
		}

		/* create new rule to clear fuckup */
		new_rule = ExprRule::createRule(
			br.getToActual(),
			br.getToExpected());
		if (new_rule == NULL)
			continue;

		if (checkRule(new_rule, s, std::cerr) == false) {
			delete new_rule;
			continue;
		}

		new_rule->printBinaryRule(of);
		of.flush();

		new_rule_c++;
		if (NoisyXtive)	new_rule->print(std::cerr);
		delete new_rule;
	}

	return new_rule_c;
}

static void findReplacementsInFrom(
	RuleBuilder* rb,
	rule_replace_ty& replacements)
{
	ExprBuilder	*init_eb;
	unsigned	i = 0;

	init_eb = Expr::getBuilder();
	foreach (it, rb->begin(), rb->end()) {
		const ExprRule	*er(*it);
		BuiltRule	br(init_eb, rb, er);

		i++;

		/* translation of from->to worked perfectly? great! */
		if (br.builtAsExpected())
			continue;

		if (br.isBetter() || (!br.isWorse() && br.fewerBits())) {
			/* translated from-expr better than the rule's to-expr;
			 * make improvement explicit. */
			replacements.push_back(
				std::make_pair(it, br.getToActual()));
			continue;
		}
	}
}

static void findDestReplacements(
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
		bool		fixed_up = false;

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
			fixed_up = true;
		}

		/* transitive rule does not reduce nodes? */
		if (	ExprUtil::getNumNodes(rb_to_expr) >=
			ExprUtil::getNumNodes(old_to_expr))
		{
			continue;
		}

		std::cerr << "Xtive [" << i << "]:";
		if (fixed_up) std::cerr << " (fixedup)";
		std::cerr << '\n';

		if (NoisyXtive) {
		er->print(std::cout);
		std::cerr	<< "FROM-EXPR: " << er->getFromExpr() << '\n'
				<< "OLD-TO-EXPR: " << old_to_expr << '\n'
				<< "NEW-TO-EXPR: " << rb_to_expr << '\n';
		}
		replacements.push_back(std::make_pair(it, rb_to_expr));
	}
}

void appendReplacements(
	RuleBuilder* rb,
	Solver* s,
	const rule_replace_ty& repl,
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

		if (NoisyXtive)
			xtive_er->print(std::cout);

		if (getRuleCex(xtive_er, s, std::cerr) == false) {
			/* don't add rule if it doesn't work */
			std::cerr << "Checking Initial Rule.\n";
			if (getRuleCex(er, s, std::cerr))
				std::cerr << "INITIAL RULE IS OK.\n";
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
	unsigned		new_from_c;

	rb = RuleBuilder::create(ExprBuilder::create(BuilderKind));

	/* find rules where building expr with rb is better than to-expr */
	/* from experience, these usually turn out to be bugs */
	findReplacementsInFrom(rb, replacements);
	std::cerr << "Got src replace count=" << replacements.size() << '\n';

	/* find rules where building to-expr with rulebuilder is better
	 * than to-expr from rule */
	findDestReplacements(eb, rb, replacements);

	std::cout << "after dest, found " << replacements.size() << '\n';

	/* create rules when rule builder generates a different
	 * from-expr where |from-expr| > |to-expr| */
	new_from_c = appendNewFroms(s, rb);

	// append new rules to brule file
	std::cout << "Appending "
		<< replacements.size() + new_from_c
		<< " rules.\n";
	appendReplacements(rb, s, replacements, bad_repl);

	// erase all superseded rules (ignoring ones that weren't valid)
	std::cout << "Superseding replacements. bad_repl="
		<< bad_repl.size() << '\n';
	foreach (it, replacements.begin(), replacements.end()) {
		const ExprRule	*er = *(it->first);
		if (bad_repl.count(er)) {
			/* failed to replace parent rule, do not erase! */
			continue;
		}
		rb->eraseDBRule(*(it->first));
	}

	delete rb;
}

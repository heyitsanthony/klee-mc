#include <unistd.h>
#include <iostream>
#include <sstream>

#include "../../lib/Expr/ExprRule.h"
#include "../../lib/Expr/RuleBuilder.h"

#include "static/Sugar.h"
#include "klee/ExprBuilder.h"
#include "klee/Solver.h"
#include "klee/util/ExprUtil.h"

using namespace klee;

extern int WorkerForks;
extern ExprBuilder::BuilderKind	BuilderKind;
extern bool checkRule(const ExprRule* er, Solver* s, std::ostream&);

static ExprRule* rebuildBRule(
	Solver *s,
	const ExprRule* er,
	std::ostream& of)
{
	std::stringstream	ss;
	ExprRule		*er_rebuild;

	if (checkRule(er, s, std::cout) == false) {
		std::cerr << "BAD RULE:\n";
		er->print(std::cerr);
		return NULL;
	}

	er->printBinaryRule(ss);
	er_rebuild = ExprRule::loadBinaryRule(ss);

	/* ensure we haven't corrupted the from-expr--
	 * otherwise, it might not match during runtime! */
	if (	ExprUtil::getNumNodes(er_rebuild->getFromExpr()) !=
		ExprUtil::getNumNodes(er->getFromExpr()))
	{
		std::cerr << "BAD REBUILD:\n";
		std::cerr << "ORIGINAL:\n";
		er->print(std::cerr);
		std::cerr << "NEW:\n";
		er_rebuild->print(std::cerr);

		std::cerr	<< "ORIG-EXPR: "
				<< er->getFromExpr() << '\n'
				<< "NEW-EXPR: "
				<< er_rebuild->getFromExpr() << '\n';
		delete er_rebuild;
		return NULL;
	}

	return er_rebuild;
}

#if 0
static void rebuildBRulesFork(Solver* s, const std::string& Input)
{
	for (unsigned ai = 0; i < WorkerForks; i++) {
		fork()
	}
}
#endif

void rebuildBRules(Solver* s, const std::string& Input)
{
	std::ofstream	of(Input.c_str());
	ExprRuleSet	ers;
	RuleBuilder	*rb;
	unsigned	i;

	assert (of.good() && !of.fail());

	rb = RuleBuilder::create(ExprBuilder::create(BuilderKind));

	i = 0;
	foreach (it, rb->begin(), rb->end()) {
		ExprRule*	er_rebuild;

		std::cerr << "[" << ++i << "]: ";
		er_rebuild = rebuildBRule(s, *it, of);
		if (er_rebuild == NULL)
			continue;

		/* only emit if hasn't seen rule before */
		if (!ers.count(er_rebuild)) {
			er_rebuild->printBinaryRule(of);
			ers.insert(er_rebuild);
		}

		delete er_rebuild;
	}

	delete rb;
}

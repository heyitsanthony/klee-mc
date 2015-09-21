#include "klee/Expr.h"
#include "klee/ExprBuilder.h"
#include "klee/Solver.h"
#include "klee/util/ExprUtil.h"
#include "../../lib/Expr/RuleBuilder.h"
#include "../../lib/Expr/ExprRule.h"
#include "../../lib/Expr/AssignHash.h"
#include <iostream>

using namespace klee;

extern ExprBuilder::BuilderKind	BuilderKind;

typedef std::map<uint64_t, std::set<ref<Expr> > > ah2expr_ty;
typedef std::map<ref<Expr>, ref<Expr> > xlatemap_ty;

bool getRuleCex(const ExprRule* er, Solver* s, std::ostream&);

static void collectHashes(RuleBuilder* rb, ah2expr_ty& ah2expr)
{
	std::set<ref<Expr> >	e_bucket[128];
	std::set<uint64_t>	ah_bucket[128];

	for (const auto er : *rb) {
		ref<Expr>		to_e;
		unsigned		w;
		uint64_t		ah;
		ah2expr_ty::iterator	ahit;

		to_e = er->getToExpr();
		w = to_e->getWidth();
		assert (w > 0 && w <= 128);
		if (e_bucket[w-1].count(to_e))
			continue;
		e_bucket[w-1].insert(to_e);

		ah = AssignHash::getEvalHash(to_e);
		ahit = ah2expr.find(ah);
		if (ahit == ah2expr.end()) {
			ahit = ah2expr.insert(
				std::make_pair(
					ah, std::set<ref<Expr> >())).first;
		}
		((*ahit).second).insert(to_e);
		ah_bucket[w-1].insert(ah);
	}

	for (unsigned i = 0; i < 128; i++)
		std::cout	<< i+1 << ' ' << e_bucket[i].size() << ' '
				<< ah_bucket[i].size() << '\n';

}

static void analyzeSampleHashSet(
	Solver* solver,
	const std::set<ref<Expr> > &s,
	xlatemap_ty	&xlate)
{
	ref<Expr>	smallest;
	unsigned	smallest_n;

	if (s.size() == 1)
		return;

	std::cout << "============SIZE: " << s.size() << '\n';
	for (const auto &cur : s) {
		unsigned	cur_n;

		if (smallest.isNull()) {
			smallest = cur;
			smallest_n = ExprUtil::getNumNodes(smallest);
		}

		cur_n = ExprUtil::getNumNodes(cur);
		
		if (cur_n < smallest_n || 
			(cur_n == smallest_n &&
			cur->hash() < smallest->hash()))
		{
			smallest_n = cur_n;
			smallest = cur;
		}

		std::cout << cur << '\n';
	}
	std::cout << "SMALLEST: " << smallest << '\n';

	for (const auto &cur : s) {
		bool		mbt;

		if (smallest == cur)
			continue;

		if (!solver->mustBeTrue(Query(MK_EQ(cur, smallest)), mbt))
			continue;

		if (mbt == false)
			continue;

		std::cout << "MATCHED: " << cur << '\n';
		xlate.insert(std::make_pair(cur, smallest));
	}
}

void normalFormCanonicalize(Solver *solver)
{
	RuleBuilder			*rb;
	ah2expr_ty			ah2expr;
	xlatemap_ty			xlate;
	std::set<const ExprRule*>	repl_set;

	std::cout << "# bits exprs samplehashes\n";

	rb = RuleBuilder::create(ExprBuilder::create(BuilderKind));
	collectHashes(rb, ah2expr);

	for (const auto &p : ah2expr) {
		const std::set<ref<Expr> > &s(p.second);
		analyzeSampleHashSet(solver, s, xlate);
	}

	std::ofstream	of(
		rb->getDBPath().c_str(),
		std::ios_base::out |
		std::ios_base::app |
		std::ios_base::binary);
	for (const auto er : *rb) {
		ExprRule		*new_er;
		ref<Expr>		to_e(er->getToExpr());
		xlatemap_ty::iterator	xit;

		xit = xlate.find(to_e);
		if (xit == xlate.end())
			continue;

		new_er = ExprRule::changeDest(er, xit->second);
		if (new_er == NULL)
			continue;

		if (getRuleCex(new_er, solver, std::cerr) == false) {
			/* don't add rule if it doesn't work */
			std::cerr << "Checking Initial Rule.\n";
			if (getRuleCex(er, solver, std::cerr))
				std::cerr << "INITIAL RULE IS OK.\n";
			continue;
		}

		repl_set.insert(er);
		new_er->printBinaryRule(of);
	}
	of.close();

	for (const auto repl : repl_set)
		rb->eraseDBRule(repl);

	delete rb;
}

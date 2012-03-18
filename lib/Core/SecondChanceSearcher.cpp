#include <llvm/Support/CommandLine.h>
#include "static/Sugar.h"
#include "SecondChanceSearcher.h"
#include "CoreStats.h"

using namespace klee;

namespace
{
	llvm::cl::opt<unsigned> SecondChanceBoost(
		"second-chance-boost",
		llvm::cl::desc("Number of extra quanta for second-chance."),
		llvm::cl::init(1));
}

SecondChanceSearcher::SecondChanceSearcher(Searcher* _searcher_base)
: searcher_base(_searcher_base)
, last_current(0)
, remaining_quanta(0)
, last_ins_cov(0)
{}

static uint64_t totalIns(void)
{
	return stats::coveredInstructions + stats::uncoveredInstructions;
}

ExecutionState& SecondChanceSearcher::selectState(bool allowCompact)
{
	assert (searcher_base->empty() == false);

	if (last_current != NULL) {
		if (last_ins_cov < totalIns()) {
			remaining_quanta += SecondChanceBoost;
			last_ins_cov = totalIns();
			std::cerr << "MORE. QUANTA=" << remaining_quanta << '\n';
		}

		if (remaining_quanta) {
			remaining_quanta--;
			std::cerr << "YOU GOT THE SECOND CHANCE ST="
				<< (void*)last_current
				<< ". QUANTA=" << remaining_quanta << '\n';
			return *last_current;
		}
	}

	remaining_quanta = 0;
	last_current = &searcher_base->selectState(allowCompact);
	last_ins_cov = totalIns();

	return *last_current;
}

void SecondChanceSearcher::update(ExecutionState *current, States s)
{
	if (s.getRemoved().count(last_current)) {
		last_current = NULL;
		last_ins_cov = totalIns();
		remaining_quanta = 0;
	}

	searcher_base->update(current, s);
}

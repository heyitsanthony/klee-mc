#include <llvm/Support/CommandLine.h>
#include "../Core/CoreStats.h"
#include "SecondChanceSearcher.h"
#include "static/Sugar.h"

using namespace klee;

namespace
{
	llvm::cl::opt<unsigned> SecondChanceBoost(
		"second-chance-boost",
		llvm::cl::desc("Number of extra quanta for second-chance."),
		llvm::cl::init(1));

	llvm::cl::opt<unsigned> SecondChanceBoostCov(
		"second-chance-boost-cov",
		llvm::cl::desc("Number of extra quanta for second-chance."),
		llvm::cl::init(1));
}

static uint64_t totalIns(void)
{ return stats::coveredInstructions + stats::uncoveredInstructions; }

ExecutionState* SecondChanceSearcher::selectState(bool allowCompact)
{
	if (last_current != NULL) {
		if (last_ins_total < totalIns())
			remaining_quanta += SecondChanceBoost;

		if (last_ins_cov < stats::coveredInstructions)
			remaining_quanta += SecondChanceBoostCov;

		if (remaining_quanta > 0) {
			remaining_quanta--;
			std::cerr << "YOU GOT THE SECOND CHANCE ST="
				<< (void*)last_current
				<< ". QUANTA=" << remaining_quanta << '\n';
			updateInsCounts();
			return last_current;
		}
	}

	/* find new state */
	remaining_quanta = 0;
	last_current = searcher_base->selectState(allowCompact);
	updateInsCounts();

	return last_current;
}

void SecondChanceSearcher::updateInsCounts(void)
{
	last_ins_total = totalIns();
	last_ins_cov = stats::coveredInstructions;
}

void SecondChanceSearcher::update(ExecutionState *current, States s)
{
	if (s.getRemoved().count(last_current)) {
		last_current = NULL;
		updateInsCounts();
		remaining_quanta = 0;
	}

	searcher_base->update(current, s);
}

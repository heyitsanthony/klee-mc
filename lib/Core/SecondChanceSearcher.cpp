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

ExecutionState& SecondChanceSearcher::selectState(bool allowCompact)
{
	assert (searcher_base->empty() == false);

	if (last_current != NULL) {
		if (last_ins_cov < stats::coveredInstructions) {
			remaining_quanta += SecondChanceBoost;
			last_ins_cov = stats::coveredInstructions;
		}

		if (remaining_quanta) {
			remaining_quanta--;
			return *last_current;
		}
	}

	remaining_quanta = 0;
	last_current = &searcher_base->selectState(allowCompact);

	return *last_current;
}

void SecondChanceSearcher::update(ExecutionState *current, States s)
{
	if (s.getRemoved().count(last_current))
		last_current = NULL;

	if (	last_current != NULL &&
		last_ins_cov < stats::coveredInstructions)
	{
		remaining_quanta += SecondChanceBoost;
		last_ins_cov = stats::coveredInstructions;
	}

	searcher_base->update(current, s);
}

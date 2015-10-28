#include <llvm/Support/CommandLine.h>
#include "../Core/CoreStats.h"
#include "../Core/Executor.h"
#include "KillCovSearcher.h"
#include "static/Sugar.h"

using namespace klee;

namespace
{
	llvm::cl::opt<unsigned> KillCovMercy(
		"killcov-mercy",
		llvm::cl::desc("Number of states to pass without new coverage."),
		llvm::cl::init(1));
}

KillCovSearcher::KillCovSearcher(Executor& _exe, Searcher* _searcher_base)
: exe(_exe)
, searcher_base(_searcher_base)
, last_current(0)
, last_ins_total(0)
, last_ins_cov(0)
, forked_current(0)
, found_instructions(false)
{}

static uint64_t totalIns(void)
{ return stats::coveredInstructions + stats::uncoveredInstructions; }


void KillCovSearcher::killForked(ExecutionState* new_st)
{
	sid_t				fork_sid;
	std::vector<ExecutionState*>	to_kill;
	unsigned			fb_c = 0;

	assert (forked_current != NULL);
	fork_sid = forked_current->getSID();

	/* NB: all forked states have sid > forked sid */
	foreach (it, exe.beginStates(), exe.endStates()) {
		ExecutionState	*st(*it);

		if (st == new_st) continue;
		if (fork_sid >= st->getSID()) continue;
		if (st->isOnFreshBranch()) {
			fb_c++;
			continue;
		}

		to_kill.push_back(st);
	}
	
	if (to_kill.size() > 1) {
		foreach (it, to_kill.begin(), to_kill.end())
			exe.terminate(*(*it));
		std::cerr << "[KillCov] No improvement. Killed " 
			<< to_kill.size() << " states. fb="
			<< fb_c << "\n";
	}

	if (	(to_kill.size() <= 1 || last_current == NULL) &&
		new_st != forked_current)
	{
		exe.terminate(*forked_current);
	}
}

ExecutionState* KillCovSearcher::selectState(bool allowCompact)
{
	ExecutionState	*new_st;

	found_instructions |= (
		last_ins_total < totalIns() || 
		last_ins_cov < stats::coveredInstructions);

	updateInsCounts();

	/* find new state */
	new_st = searcher_base->selectState(allowCompact);
	if (new_st == nullptr)
		return nullptr;

	/* new state? */
	if (new_st != last_current) {
		std::cerr << "[KillCov] Total states = " << exe.getNumStates()
			<< '\n';
		if (!found_instructions && forked_current) {
			killForked(new_st);
		} else if (forked_current) {
			if (forked_current != new_st)
				exe.terminate(*forked_current);
			else
				std::cerr << "[KillCov] Warning, selected fork.\n";
			std::cerr << "[KillCov] Forked states OK.\n";
		}

		if (forked_current != new_st)
			forked_current = exe.pureFork(*new_st);
		else
			forked_current = NULL;
		found_instructions = false;
	}

	updateInsCounts();

	last_current = new_st;
	return last_current;
}

void KillCovSearcher::updateInsCounts(void)
{
	last_ins_total = totalIns();
	last_ins_cov = stats::coveredInstructions;
}

void KillCovSearcher::update(ExecutionState *current, States s)
{
	searcher_base->update(current, s);

	if (!s.getRemoved().count(last_current))
		return;

	found_instructions |= (
		last_ins_total < totalIns() || 
		last_ins_cov < stats::coveredInstructions);


	last_current = NULL;
#if 0
	if (forked_current != NULL) {
		std::cerr << "[KillCov] Killing forked current\n";
		if (!found_instructions) {
			killForked(NULL);
		} else {
			/* keep forked states, kill snapshot */
			exe.terminate(*forked_current);
		}
	}
	forked_current = NULL;
	found_instructions = false;
#endif
	updateInsCounts();
}

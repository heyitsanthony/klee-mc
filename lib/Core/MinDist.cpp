/* It's not obvoius how this code works, so I assume it's broken and/or wrong.
 * -AJR */

#include "CoreStats.h"
#include "ExeStateManager.h"
#include "Executor.h"
#include "klee/Statistics.h"
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/CallSite.h>
#include "klee/ExecutionState.h"
#include "StatsTracker.h"
#include "klee/Internal/Support/ModuleUtil.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Module/KInstruction.h"
#include "static/Sugar.h"

#include <map>

using namespace klee;
using namespace llvm;

typedef std::map<Instruction*, std::vector<Function*> > calltargets_ty;

static calltargets_ty callTargets;
static std::map<Function*, std::vector<Instruction*> > functionCallers;
static std::map<Function*, unsigned> functionShortestPath;

static std::vector<Instruction*> getSuccs(Instruction *i)
{
	BasicBlock			*bb;
	std::vector<Instruction*>	res;

	bb = i->getParent();
	if (i==bb->getTerminator()) {
		foreach (it, succ_begin(bb), succ_end(bb))
			res.push_back(it->begin());
	} else {
		res.push_back(++BasicBlock::iterator(i));
	}

	return res;
}

uint64_t klee::computeMinDistToUncovered(
	const KInstruction *ki,
	uint64_t minDistAtRA)
{
	StatisticManager &sm = *theStatisticManager;

	assert (ki != NULL && "BAD KI ON COMPUTE MIN DIST");

	// unreachable on return, best is local
	if (minDistAtRA==0)
		return sm.getIndexedValue(
			stats::minDistToUncovered, ki->getInfo()->id);

	uint64_t minDistLocal, distToReturn;

	minDistLocal = sm.getIndexedValue(
		stats::minDistToUncovered, ki->getInfo()->id);
	distToReturn = sm.getIndexedValue(
		stats::minDistToReturn, ki->getInfo()->id);


	// return unreachable, best is local
	if (distToReturn==0) return minDistLocal;

	// no local reachable
	if (!minDistLocal) return distToReturn + minDistAtRA;

	return std::min(minDistLocal, distToReturn + minDistAtRA);
}

bool StatsTracker::init = true;

// Compute call targets for a func. It would be nice to use alias info
// instead of assuming all indirect calls hit all escaping
// functions, eh?
void StatsTracker::computeCallTargets(Function* fnIt)
{
	foreach (bbIt, fnIt->begin(), fnIt->end()) {
	foreach (it, bbIt->begin(), bbIt->end()) {

		if (!isa<CallInst>(it) && !isa<InvokeInst>(it))
			continue;

		CallSite cs(it);
		if (isa<InlineAsm>(cs.getCalledValue())) {
			// We can never call through here so assume no targets
			// (which should be correct anyhow).
			callTargets.insert(
				std::make_pair(it, std::vector<Function*>()));
		} else if (Function *target = getDirectCallTarget(cs)) {
			callTargets[it].push_back(target);
		} else {
			callTargets[it] =
			  std::vector<Function*>(
			  	km->escapingFunctions.begin(),
				km->escapingFunctions.end());
		}
	}
	}
}

void StatsTracker::initMinDistToReturn(
	Function* fnIt,
	std::vector<Instruction* >& instructions)
{
	const InstructionInfoTable &infos = *km->infos;
	StatisticManager &sm = *theStatisticManager;

	if (fnIt->isDeclaration()) {
		if (fnIt->doesNotReturn()) {
			functionShortestPath[fnIt] = 0;
		} else {
			// whatever
			functionShortestPath[fnIt] = 1;
		}
	} else {
		functionShortestPath[fnIt] = 0;
	}

	// Not sure if I should bother to preorder here. XXX I should.
	foreach (bbIt, fnIt->begin(), fnIt->end()) {
	foreach (it, bbIt->begin(), bbIt->end()) {
		instructions.push_back(it);
		unsigned id = infos.getInfo(it).id;
		sm.setIndexedValue(
			stats::minDistToReturn,
			id,
			isa<ReturnInst>(it) /*  || isa<UnwindInst>(it) */);
	}
	}
}

bool StatsTracker::computePathsInit(std::vector<Instruction*>& instructions)
{
	const InstructionInfoTable	&infos(*km->infos);
	StatisticManager		&sm(*theStatisticManager);
	bool				changed = false;

	foreach (it, instructions.begin(), instructions.end()) {
		Instruction *inst = *it;
		unsigned bestThrough = 0;

		if (isa<CallInst>(inst) || isa<InvokeInst>(inst)) {
			std::vector<Function*> &targets = callTargets[inst];
			foreach (fnIt, targets.begin(), targets.end()) {
				uint64_t dist;

				dist = functionShortestPath[*fnIt];
				if (!dist) continue;

				// count instruction itself
				dist = 1+dist;
				if (bestThrough==0 || dist<bestThrough)
					bestThrough = dist;
			}
		} else {
			bestThrough = 1;
		}

		if (!bestThrough)
			continue;

		unsigned id;
		uint64_t best, cur;

		id = infos.getInfo(*it).id;
		cur = best = sm.getIndexedValue(stats::minDistToReturn, id);
		std::vector<Instruction*> succs = getSuccs(*it);

		foreach (it2, succs.begin(), succs.end()) {
			uint64_t dist, val;

			dist = sm.getIndexedValue(
				stats::minDistToReturn,
				infos.getInfo(*it2).id);

			if (!dist) continue;

			val = bestThrough + dist;
			if (best == 0 || val < best)
				best = val;
		}

		if (best == cur)
			continue;

		sm.setIndexedValue(stats::minDistToReturn, id, best);
		changed = true;

		// Update shortest path if this is the entry point.
		Function *f = inst->getParent()->getParent();
		if (inst==f->begin()->begin())
			functionShortestPath[f] = best;
	}

	return changed;
}

void StatsTracker::computeReachableUncoveredInit(void)
{
	Module *m = km->module.get();

	assert (init);
	init = false;

	for (auto &fn : *m) computeCallTargets(&fn);

	// Compute function callers as reflexion of callTargets.
	for (auto &tgt : callTargets) {
		for (auto &f : tgt.second) {
			functionCallers[f].push_back(tgt.first);
		}
	}

	// Initialize minDistToReturn to shortest paths through
	// functions. 0 is unreachable.
	std::vector<Instruction *> instructions;
	for (auto &fn : *m) initMinDistToReturn(&fn, instructions);

	std::reverse(instructions.begin(), instructions.end());

	while (computePaths(instructions));
}

bool StatsTracker::computePaths(std::vector<llvm::Instruction*>& insts)
{
	const InstructionInfoTable	&infos(*km->infos);
	StatisticManager		&sm(*theStatisticManager);
	bool				changed = false;

	foreach (it, insts.begin(), insts.end()) {
		Instruction *inst = *it;
		uint64_t best, cur;
		unsigned bestThrough = 0;

		cur = best = sm.getIndexedValue(
			stats::minDistToUncovered,
			infos.getInfo(inst).id);

		if (isa<CallInst>(inst) || isa<InvokeInst>(inst)) {
		std::vector<Function*> &targets = callTargets[inst];
		foreach (fnIt, targets.begin(), targets.end()) {
			uint64_t	dist, calleeDist;


			dist = functionShortestPath[*fnIt];
			if (dist) {
				// count instruction itself
				dist = 1+dist;
				if (bestThrough==0 || dist<bestThrough)
					bestThrough = dist;
			}

			if ((*fnIt)->isDeclaration())
				continue;

			calleeDist = sm.getIndexedValue(
				stats::minDistToUncovered,
				infos.getFunctionInfo(*fnIt).id);
			if (calleeDist) {
				// count instruction itself
				calleeDist = 1+calleeDist;
				if (best == 0 || calleeDist<best)
					best = calleeDist;
			}
		}
		} else {
			bestThrough = 1;
		}

		if (bestThrough) {
			std::vector<Instruction*> succs = getSuccs(inst);
			foreach (it2, succs.begin(), succs.end()) {
				uint64_t dist;

				dist = sm.getIndexedValue(
					stats::minDistToUncovered,
					infos.getInfo(*it2).id);
				if (dist) {
					uint64_t val = bestThrough + dist;
					if (best==0 || val<best)
					best = val;
				}
			}
		}

		if (best != cur) {
			sm.setIndexedValue(
				stats::minDistToUncovered,
				infos.getInfo(inst).id,
				best);
			changed = true;
		}
	}

	return changed;
}

void StatsTracker::computeReachableUncovered()
{
	Module *m = km->module.get();
	const InstructionInfoTable &infos = *km->infos;
	StatisticManager &sm = *theStatisticManager;

	if (init) computeReachableUncoveredInit();

	// compute minDistToUncovered, 0 is unreachable
	std::vector<Instruction *> instructions;
	for (auto &fn : *m) 
	// Not sure if I should bother to preorder here.
	for (auto &bb : fn) 
	for (auto &ii : bb) {
		unsigned id = infos.getInfo(&ii).id;
		instructions.push_back(&ii);
		sm.setIndexedValue(
			stats::minDistToUncovered,
			id,
			sm.getIndexedValue(stats::uncoveredInstructions, id));
	}

	std::reverse(instructions.begin(), instructions.end());

	while (computePaths(instructions));

	foreach (
		it,
		executor.stateManager->begin(),
		executor.stateManager->end())
	{
		ExecutionState *es = *it;
		uint64_t currentFrameMinDist = 0;

		foreach (sfIt, es->stack.begin(), es->stack.end()) {
			CallStack::iterator next = sfIt + 1;
			KInstIterator kii;

			sfIt->minDistToUncoveredOnReturn = currentFrameMinDist;

			if (next == es->stack.end()) {
				kii = es->pc;
			} else {
				kii = next->caller;
				++kii;
	/* XXX this is to get vexllvm working,
	* the problem here is that we want to figure out where
	* we jump to after a call the caller function terminates with a return
	* jumping to the current state (e.g. kii+1 is after the caller's retrn)
	* Going to have to try something different for DBT */
				if ((const KInstruction*)kii == NULL)
					continue;
			}

			currentFrameMinDist = computeMinDistToUncovered(
				kii, currentFrameMinDist);
		}
	}
}

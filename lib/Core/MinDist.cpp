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
#include <unordered_map>

using namespace klee;
using namespace llvm;

typedef std::unordered_map<const Instruction*, std::vector<Function*> > calltargets_ty;

static calltargets_ty callTargets;
static std::map<Function*, std::vector<const Instruction*> > functionCallers;
static std::unordered_map<const Function*, unsigned> functionShortestPath;

static std::vector<const Instruction*> getSuccs(const Instruction *i)
{
	std::vector<const Instruction*>	res;

	auto bb = i->getParent();
	if (i==bb->getTerminator()) {
		foreach (it, succ_begin(bb), succ_end(bb))
			res.push_back(&(*(it->begin())));
	} else {
		auto it = ++BasicBlock::const_iterator(i);
		res.push_back(&(*it));
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
	for (const auto& inst : *bbIt) {
		if (!isa<CallInst>(&inst) && !isa<InvokeInst>(&inst))
			continue;

		CallSite cs(const_cast<Instruction*>(&inst));
		Value	*v = cs.getCalledValue();
		if (isa<InlineAsm>(v)) {
			// We can never call through here so assume no targets
			// (which should be correct anyhow).
			callTargets.emplace(&inst, std::vector<Function*>());
		} else if (Function *target = getDirectCallTarget(v)) {
			callTargets[&inst].push_back(target);
		} else {
			callTargets[&inst] =
			  std::vector<Function*>(
			  	km->escapingFunctions.begin(),
				km->escapingFunctions.end());
		}
	}
	}
}

void StatsTracker::initMinDistToReturn(
	Function* fnIt,
	std::vector<const Instruction* >& instructions)
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
	for (const auto &inst : *bbIt) {
		instructions.push_back(&inst);
		unsigned id = infos.getInfo(&inst).id;
		sm.setIndexedValue(
			stats::minDistToReturn,
			id,
			isa<const ReturnInst>(&inst) /*  || isa<UnwindInst>(it) */);
	}
	}
}

bool StatsTracker::computePathsInit(std::vector<const Instruction*>& instructions)
{
	const InstructionInfoTable	&infos(*km->infos);
	StatisticManager		&sm(*theStatisticManager);
	bool				changed = false;

	for (auto inst : instructions) {
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

		id = infos.getInfo(inst).id;
		cur = best = sm.getIndexedValue(stats::minDistToReturn, id);
		auto succs = getSuccs(inst);

		for (auto inst : succs) {
			uint64_t dist, val;

			dist = sm.getIndexedValue(
				stats::minDistToReturn,
				infos.getInfo(inst).id);

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
		const Function *f = inst->getParent()->getParent();
		if (inst == &(*f->begin()->begin()))
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
	std::vector<const Instruction *> instructions;
	for (auto &fn : *m) initMinDistToReturn(&fn, instructions);

	std::reverse(instructions.begin(), instructions.end());

	while (computePaths(instructions));
}

bool StatsTracker::computePaths(std::vector<const llvm::Instruction*>& insts)
{
	const InstructionInfoTable	&infos(*km->infos);
	StatisticManager		&sm(*theStatisticManager);
	bool				changed = false;

	for (auto inst : insts) {
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
			for (auto succInst : getSuccs(inst)) {
				uint64_t dist;

				dist = sm.getIndexedValue(
					stats::minDistToUncovered,
					infos.getInfo(succInst).id);
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
	std::vector<const Instruction *> instructions;
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

	for (const auto es : *executor.stateManager) {
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

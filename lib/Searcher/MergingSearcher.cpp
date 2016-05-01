#include <llvm/IR/Instructions.h>
#include <llvm/IR/CallSite.h>
#include "klee/Internal/Module/KModule.h"
#include "../Core/Executor.h"

#include "MergingSearcher.h"

#include "static/Sugar.h"

using namespace llvm;
using namespace klee;

MergingSearcher::MergingSearcher(ExecutorBC &_executor, Searcher *_baseSearcher)
: executor(_executor)
, baseSearcher(_baseSearcher)
, mergeFunction(executor.getKModule()->kleeMergeFn)
{}

MergingSearcher::~MergingSearcher()
{ delete baseSearcher; }

const Instruction *MergingSearcher::getMergePoint(const ExecutionState &es)
{
	if (!mergeFunction) return 0;

	const Instruction *i = es.pc->getInst();

	if (i->getOpcode() != Instruction::Call)
		return 0;

	CallSite cs(const_cast<CallInst*>(cast<const CallInst>(i)));
	if (mergeFunction == cs.getCalledFunction())
		return i;

	return 0;
}

ExecutionState *MergingSearcher::selectState(bool allowCompact)
{
	ExecutionState	*es;

	while ((es = baseSearcher->selectState(allowCompact))) {
		assert (es->checkCanary());

		if (getMergePoint(*es) == nullptr)
			return es;

		baseSearcher->removeState(es, es);
		statesAtMerge.insert(es);
	}

	// build map of merge point -> state list
	std::map<const Instruction*, std::vector<ExecutionState*> > merges;
	for (auto es : statesAtMerge) {
		merges[getMergePoint(*es)].push_back(es);
	}

	for (const auto& p : merges) {
		// merge states
		ExeStateSet toMerge(p.second.begin(), p.second.end());
		while (!toMerge.empty()) {
			ExeStateSet	toErase;
			ExecutionState	*base;

			base = *toMerge.begin();
			toMerge.erase(toMerge.begin());

			for (auto mergeWith : toMerge) {
				if (base->merge(*mergeWith))
					toErase.insert(mergeWith);
			}

			for (auto ers : toErase) {
				ExeStateSet::iterator it2 = toMerge.find(ers);
				assert(it2 != toMerge.end());
				executor.terminate(*ers);
				toMerge.erase(it2);
			}

			// step past merge and toss base back in pool
			statesAtMerge.erase(base);
			//++base->pc; ??? AJR
			baseSearcher->addState(base);
		}
	}

	return baseSearcher->selectState(allowCompact);
}

void MergingSearcher::update(ExecutionState *current, const States s)
{
	if (s.getRemoved().empty()) {
		baseSearcher->update(current, s);
		return;
	}

	ExeStateSet	alt = s.getRemoved();

	foreach (it, s.getRemoved().begin(), s.getRemoved().end()) {
		ExecutionState *es = *it;
		ExeStateSet::const_iterator c_it;

		assert (es->checkCanary());

		c_it = statesAtMerge.find(es);
		if (c_it == statesAtMerge.end())
			continue;

		statesAtMerge.erase(c_it);
		alt.erase(es);
	}

	// any state in statesAtMerge has already been removed from 
	// the base searcher, only dispatch nonmembers
	baseSearcher->update(current, States(s.getAdded(), alt));
}

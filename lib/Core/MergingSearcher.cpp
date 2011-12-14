#include "llvm/Instructions.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/CommandLine.h"
#include "klee/Internal/Module/KModule.h"
#include "Executor.h"

#include "MergingSearcher.h"

#include "static/Sugar.h"

using namespace llvm;
using namespace klee;

namespace {
  cl::opt<bool>
  DebugLogMerge("debug-log-merge");
}

MergingSearcher::MergingSearcher(ExecutorBC &_executor, Searcher *_baseSearcher)
: executor(_executor)
, baseSearcher(_baseSearcher)
, mergeFunction(executor.getKModule()->kleeMergeFn) {}

MergingSearcher::~MergingSearcher() {
  delete baseSearcher;
}

Instruction *MergingSearcher::getMergePoint(ExecutionState &es)
{
  if (!mergeFunction) return 0;

  Instruction *i = es.pc->getInst();

  if (i->getOpcode() == Instruction::Call) {
    CallSite cs(cast<CallInst > (i));
    if (mergeFunction == cs.getCalledFunction())
      return i;
  }
  return 0;
}

ExecutionState &MergingSearcher::selectState(bool allowCompact)
{
  while (!baseSearcher->empty()) {
    ExecutionState &es = baseSearcher->selectState(allowCompact);
    if (getMergePoint(es)) {
      baseSearcher->removeState(&es, &es);
      statesAtMerge.insert(&es);
    } else {
      return es;
    }
  }

  // build map of merge point -> state list
  std::map<Instruction*, std::vector<ExecutionState*> > merges;
  foreach (it, statesAtMerge.begin(), statesAtMerge.end()) {
    ExecutionState &state = **it;
    Instruction *mp = getMergePoint(state);
    merges[mp].push_back(&state);
  }

  if (DebugLogMerge)
    std::cerr << "-- all at merge --\n";
  foreach (it, merges.begin(), merges.end()) {
    if (DebugLogMerge) {
      std::cerr << "\tmerge: " << it->first << " [";
      foreach (it2, it->second.begin(), it->second.end()) {
        ExecutionState *state = *it2;
        std::cerr << state << ", ";
      }
      std::cerr << "]\n";
    }

    // merge states
    ExeStateSet toMerge(it->second.begin(), it->second.end());
    while (!toMerge.empty()) {
      ExecutionState *base = *toMerge.begin();
      toMerge.erase(toMerge.begin());

      ExeStateSet toErase;
      for (ExeStateSet::iterator it = toMerge.begin(),
              ie = toMerge.end(); it != ie; ++it) {
        ExecutionState *mergeWith = *it;

        if (base->merge(*mergeWith)) {
          toErase.insert(mergeWith);
        }
      }
      if (DebugLogMerge && !toErase.empty()) {
        std::cerr << "\t\tmerged: " << base << " with [";
        for (ExeStateSet::iterator it = toErase.begin(),
                ie = toErase.end(); it != ie; ++it) {
          if (it != toErase.begin()) std::cerr << ", ";
          std::cerr << *it;
        }
        std::cerr << "]\n";
      }

      foreach (erase_it, toErase.begin(), toErase.end()) {
        ExeStateSet::iterator it2 = toMerge.find(*erase_it);
        assert(it2 != toMerge.end());
        executor.terminateState(**erase_it);
        toMerge.erase(it2);
      }

      // step past merge and toss base back in pool
      statesAtMerge.erase(statesAtMerge.find(base));
      ++base->pc;
      baseSearcher->addState(base);
    }
  }

  if (DebugLogMerge)
    std::cerr << "-- merge complete, continuing --\n";

  return selectState(allowCompact);
}

void MergingSearcher::update(ExecutionState *current, const States s)
{
	if (s.getRemoved().empty()) {
		baseSearcher->update(current, s);
		return;
	}

	std::set<ExecutionState *> alt = s.getRemoved();
	foreach (it, s.getRemoved().begin(), s.getRemoved().end()) {
		ExecutionState *es = *it;
		ExeStateSet::const_iterator c_it = statesAtMerge.find(es);

		if (c_it == statesAtMerge.end()) continue;
		statesAtMerge.erase(c_it);
		alt.erase(alt.find(es));
	}

	baseSearcher->update(
		current, 
		States(s.getAdded(), alt, s.getIgnored(), s.getUnignored()));
}

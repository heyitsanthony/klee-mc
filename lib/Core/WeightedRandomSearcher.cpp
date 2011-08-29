#include <cassert>
#include "klee/Statistics.h"
#include "klee/Internal/ADT/RNG.h"
#include "klee/Internal/ADT/DiscretePDF.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/InstructionInfoTable.h"

#include "CoreStats.h"
#include "StatsTracker.h"
#include "Executor.h"
#include "static/Sugar.h"

#include "WeightedRandomSearcher.h"

using namespace klee;
namespace klee { extern RNG theRNG; }

WeightedRandomSearcher::WeightedRandomSearcher(Executor &_executor,
        WeightType _type)
: executor(_executor),
states(new DiscretePDF<ExecutionState*>()),
type(_type) {
  switch (type) {
    case Depth:
      updateWeights = false;
      break;
    case InstCount:
    case CPInstCount:
    case QueryCost:
    case MinDistToUncovered:
    case CoveringNew:
      updateWeights = true;
      break;
    default:
      assert(0 && "invalid weight type");
  }
}

WeightedRandomSearcher::~WeightedRandomSearcher() {
  delete states;
}

ExecutionState &WeightedRandomSearcher::selectState(bool allowCompact) {
  ExecutionState *es = states->choose(theRNG.getDoubleL(), allowCompact);
  return *es;
}

double WeightedRandomSearcher::getWeight(ExecutionState *es) {
  if (es->isCompactForm || es->isReplayDone() == false)
    return es->weight;

  switch (type) {
    default:
    case Depth:
      return es->weight;
    case InstCount:
    {
      uint64_t count = theStatisticManager->getIndexedValue(stats::instructions,
              es->pc->info->id);
      double inv = 1. / std::max((uint64_t) 1, count);
      return es->weight = inv * inv;
    }
    case CPInstCount:
    {
      StackFrame &sf = es->stack.back();
      uint64_t count = sf.callPathNode->statistics.getValue(stats::instructions);
      double inv = 1. / std::max((uint64_t) 1, count);
      return es->weight = inv;
    }
    case QueryCost:
      return es->weight = (es->queryCost < .1) ? 1. : 1. / es->queryCost;
    case CoveringNew:
    case MinDistToUncovered:
    {
      uint64_t md2u = computeMinDistToUncovered(es->pc,
              es->stack.back().minDistToUncoveredOnReturn);

      double invMD2U = 1. / (md2u ? md2u : 10000);
      if (type == CoveringNew) {
        double invCovNew = 0.;
        if (es->instsSinceCovNew)
          invCovNew = 1. / std::max(1, (int) es->instsSinceCovNew - 1000);
        return es->weight = (invCovNew * invCovNew + invMD2U * invMD2U);
      } else {
        return es->weight = invMD2U * invMD2U;
      }
    }
  }
}

void WeightedRandomSearcher::update(ExecutionState *current,
        const ExeStateSet &addedStates,
        const ExeStateSet &removedStates,
        const ExeStateSet &ignoreStates,
        const ExeStateSet &unignoreStates) {
  if (current && updateWeights && !removedStates.count(current) && !ignoreStates.count(current))
    states->update(current, getWeight(current));

  foreach (it, addedStates.begin(), addedStates.end()) {
    ExecutionState *es = *it;
    states->insert(es, getWeight(es), es->isCompactForm);
  }

  foreach (it, ignoreStates.begin(), ignoreStates.end())
    states->remove(*it);

  foreach (it, unignoreStates.begin(), unignoreStates.end()) {
    ExecutionState *es = *it;
    states->insert(es, getWeight(es), es->isCompactForm);
  }

  foreach (it, removedStates.begin(), removedStates.end())
    states->remove(*it);
}

bool WeightedRandomSearcher::empty() const { return states->empty(); }

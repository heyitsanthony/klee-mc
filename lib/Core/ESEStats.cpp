#include "ESEStats.h"
#include "Memory.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "static/AliasingRunner.h"
#include "ControlDependence.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Module/KInstruction.h"
#include "StaticRecord.h"
#include "llvm/Function.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/Support/InstIterator.h"
#include "StateRecord.h"
#include "Sugar.h"
#include "klee/Statistics.h"
#include "CoreStats.h"
#include "EquivalentStateEliminator.h"

#include <iostream>

using namespace klee;

unsigned ESEStats::updateCount(0);
ProfileTimer ESEStats::stackTimer(true);
ProfileTimer ESEStats::constraintTimer(true);
ProfileTimer ESEStats::addressSpaceTimer(true);
ProfileTimer ESEStats::checkTimer(true);
ProfileTimer ESEStats::terminateTimer(true);
ProfileTimer ESEStats::handleTimer(false);
ProfileTimer ESEStats::copyTimer(true);
ProfileTimer ESEStats::setupTimer(true);
ProfileTimer ESEStats::reviseTimer(false);
ProfileTimer ESEStats::totalTimer(false);
std::map<llvm::Instruction*, unsigned > ESEStats::forks;
unsigned ESEStats::termRecCount(0);
bool ESEStats::debug(false);
bool ESEStats::debugVerbose(false);
bool ESEStats::printStats(false);
bool ESEStats::printCoverStats(false);

void EquivalentStateEliminator::coverStats() {

  std::list<KFunction*> worklist;
  Function* main = kmodule->module->getFunction("main");
  assert(main);
  KFunction* kmain = kmodule->getKFunction(main);
  assert(kmain);
  worklist.push_back(kmain);
  std::set<KFunction*> visited;
  std::set<BasicBlock*> seenbbs;
  while (!worklist.empty()) {
    KFunction* kf = worklist.front();
    assert(kf);
    worklist.pop_front();

    if (kf->trackCoverage) {
      for (unsigned i = 0; i < kf->numInstructions; i++) {
        KInstruction* ki;
	Instruction* inst;

	ki = kf->instructions[i];
        assert(ki);
        assert(ki->info);

        inst = ki->inst;
        assert(inst);

        if (isa<UnreachableInst > (inst)) continue;

        if (theStatisticManager->getIndexedValue(
		stats::coveredInstructions, ki->info->id))
			continue;

        BasicBlock* bb = inst->getParent();
        if (seenbbs.count(bb)) continue;

        seenbbs.insert(bb);
        std::cout << "UNCOVERED: " << 
		inst->getParent()->getParent()->getNameStr() << " " <<
		inst->getParent()->getNameStr()/* << " " << *inst */ << std::endl;

        StaticRecord* rec = ki->staticRecord;
        assert(rec);
        std::cout << rec << " cover=" << rec->covered <<
		" complete=" << rec->scc->completed << std::endl;
      }
    }

    foreach(ciit, inst_begin(kf->function), inst_end(kf->function)) {
      Instruction* inst = &*ciit;
      if (CallInst * ci = dyn_cast<CallInst > (inst)) {

        foreach(it, 
		controlDependence->aliasingRunner->callgraph->callees_begin(ci),
		controlDependence->aliasingRunner->callgraph->callees_end(ci))
	{
          Function* callee = it->second;
          assert(callee);
          KFunction* kcallee = kmodule->getKFunction(callee);
          if (!kcallee) continue;

          if (visited.count(kcallee)) continue;
          worklist.push_back(kcallee);
          visited.insert(kcallee);
        }

      }
    }
  }
}

/*
static bool StateRecordComparerPtrGreaterThan(StateRecordComparer* c1, StateRecordComparer* c2) {
  return c1->timer.time() > c2->timer.time();
}*/

void EquivalentStateEliminator::stats() {

  /*
    std::vector<StateRecordComparer*> v;


    foreach(it, recCache.begin(), recCache.end()) {
      v.push_back(&it->second);
    }

    sort(v.begin(), v.end(), StateRecordComparerPtrGreaterThan);

    unsigned time = 0;
    for (unsigned i = 0; i < v.size(); i++) {
      time += v[i]->timer.time();
    }

    unsigned top20time = 0;
    for (unsigned i = 0; (i < 20) && (i < v.size()); i++) {
      StateRecordComparer* src = v[i];
      std::cout << "StateRecordComparer " << i << ": livesetcount=" << src->cache.size() << " " << (1.0 * src->cache.size() / src->recs.size()) << "%";
      std::cout << " recs=" << src->recs.size();
      std::cout << " " << (src->recs.size() * 1.0 / StateRecord::count);
      std::cout << " time=" << src->timer.time() << " " << (1.0 * src->timer.time() / time) << std::endl;
      top20time += src->timer.time();
      src->printStack();
    }
    std::cout << "top 20 time: " << top20time << " " << (top20time * 1.0 / time) << std::endl;

    if (!v.empty()) {
      StateRecordComparer* src = v[0];
      src->printLiveSetStats();
    }
   */
  ESEStats::totalTimer.stop();
  std::cout << std::endl;
  std::cout << "total time = " << ESEStats::totalTimer.time() << "\n";
  std::cout << "revise time = " << ESEStats::reviseTimer.time() << "\n";
  std::cout << "handle time = " << (ESEStats::handleTimer.time() * 1.0 / ESEStats::totalTimer.time()) << "\n";
  //std::cout << "pruning recs = " << pruningRecs.size() << " total=" << "\n";
  //std::cout << "statecache time = " << (stateCacheTimer.time() * 1.0 / reviseTimer.time()) << "\n";
  std::cout << "setup time = " << (ESEStats::setupTimer.time() * 1.0 / ESEStats::reviseTimer.time()) << "\n";
  std::cout << "conoffwr.count=" << ConOffObjectWrite::count << " " << ((ConOffObjectWrite::count * sizeof (ConOffObjectWrite)) / 1000) << "KB" << std::endl;
  std::cout << "symoffwr.count=" << SymOffObjectWrite::count << " " << ((SymOffObjectWrite::count * (sizeof (SymOffObjectWrite) + sizeof (ObjectState))) / 1000) << "KB" << std::endl;
  std::cout << "stackwr.count=" << StackWrite::count << " " << ((StackWrite::count * sizeof (StackWrite)) / 1000) << "KB" << std::endl;
  std::cout << "conoffarr.count=" << ConOffArrayAlloc::count << " " << ((ConOffArrayAlloc::count * sizeof (ConOffArrayAlloc)) / 1000) << "KB" << std::endl;
  std::cout << "symoffarr.count=" << SymOffArrayAlloc::count << " " << ((SymOffArrayAlloc::count * sizeof (SymOffArrayAlloc)) / 1000) << "KB" << std::endl;
  std::cout << "depnodeleafs =" << (DependenceNode::inleafcount * 1. / DependenceNode::count) << "%" << std::endl;
  std::cout << "statrecleafs =" << (StateRecord::inleafcount * 1. / StateRecord::count) << "%" << std::endl;
  std::cout << "depnode.count=" << DependenceNode::count << std::endl;

  std::cout << "staterec: " << "term=" << (ESEStats::termRecCount * 1.0 / StateRecord::count) << "%";
  std::cout << " total=" << StateRecord::count << " ";
  std::cout << ((StateRecord::count * sizeof (StateRecord)) / 1000) << "KB" << " " << (StateRecord::size / 1000000) << "MB" << std::endl;

  //std::cout << "allcompares2 = " << StateRecordComparer::allcompares2 << " " << (StateRecordComparer::allcompares2 * 1.0 / StateRecordComparer::allcompares) << std::endl;
  //std::cout << "done: merge time = " << (mergeTimer.time() * 1.0 / reviseTimer.time()) << "\n";
  //std::cout << "done: check time = " << (checkTimer.time() / (totalTimer.time() - reviseTimer.time())) << "\n";
  //std::cout << "done:\tstack time = " << (stackTimer.time() * 1.0 / totalTimer.time()) << "\n";
  // std::cout << "done:\tconstr time = " << (constraintTimer.time() * 1.0 / checkTimer.time()) << "\n";
  //std::cout << "done:\taddrsp time = " << (addressSpaceTimer.time() * 1.0 / checkTimer.time()) << "\n";
  //std::cout << "done:\tcopy time = " << (copyTimer.time() * 1.0 / reviseTimer.time()) << "\n";
  std::cout << "equivStateElim % = " << (ESEStats::reviseTimer.time() / ESEStats::totalTimer.time()) << "\n";
  std::cout << "equivStateElim X = " << (ESEStats::reviseTimer.time() / (ESEStats::totalTimer.time() - ESEStats::reviseTimer.time())) << "\n";
  ESEStats::totalTimer.start();
}
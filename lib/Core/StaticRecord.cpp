#include "StaticRecord.h"


#include "llvm/Module.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Module/KInstruction.h"
#include "llvm/Instructions.h"

#include "StateRecord.h"
#include "static/Support.h"
#include "Sugar.h"
#include <boost/functional/hash.hpp>

#include <iostream>
#include <fstream>

using namespace klee;

StaticRecordSCC::StaticRecordSCC() : completed(0) {
}

bool StaticRecordSCC::haveAllCovered() {

  foreach(it, elms.begin(), elms.end()) {
    StaticRecord* rec = *it;
    if (!rec->covered) {
      return false;
    }
  }
  return true;
}

bool StaticRecordSCC::haveAllSuccsCompleted() {

  foreach(it, succs.begin(), succs.end()) {
    StaticRecordSCC* succ = *it;
    if (!succ->completed)
      return false;
  }
  return true;
}

void StaticRecordSCC::print() {

  foreach(eit, elms.begin(), elms.end()) {
    StaticRecord* e = *eit;
    std::cout << "  " << e->name() << ":" << e->function->getNameStr() << ":" << e->basicBlock->getNameStr() << std::endl;
  }
}

//////////////////////////////////////////////////////////////////////////////

unsigned StaticRecord::hash(StaticRecord* r1, StaticRecord* r2) {
  size_t h = 1;
  boost::hash_combine(h, r1);
  boost::hash_combine(h, r2);
  return h;
}

bool StaticRecord::isReturn() {
  return isa<ReturnInst > (insts.back());
}

bool StaticRecord::isPHI() {
  return isa<PHINode > (insts.front());
}

bool StaticRecord::isPredicate() {
  assert(!insts.empty());
  Instruction* inst = insts.back();
  if (BranchInst * bi = dyn_cast<BranchInst > (inst)) {
    if (bi->isConditional()) {
      return true;
    }
  } else if (isa<SwitchInst > (inst)) {
    return true;
  }

  return false;
}

std::string StaticRecord::name() {
  std::string s = "rec_" + basicBlock->getNameStr() + "_" + Support::str(index);
  return s;
}

void StaticRecord::cover(bool debug) {
  if (covered) return;

  if (debug) {
    std::cout << "COVER: " << name() << " scc.size=" << scc->elms.size() << " sccsucc.size=" << scc->succs.size() << std::endl;
  }

  covered = true;

  std::vector<StateRecord*> reterminateSources;
  std::list<StaticRecordSCC*> worklist;
  assert(scc);
  worklist.push_back(scc);

  while (!worklist.empty()) {
    StaticRecordSCC* w = worklist.front();
    assert(w);
    worklist.pop_front();

    if (debug) {
      std::cout << "WORKLIST: ";
      w->print();
      std::cout << "succs:" << std::endl;
      unsigned scci = 0;

      foreach(it, w->succs.begin(), w->succs.end()) {
        StaticRecordSCC* wsucc = *it;
        std::cout << "scc#=" << (scci++) << " complete=" << wsucc->completed << std::endl;
        wsucc->print();
      }
    }

    if (w->haveAllCovered() && w->haveAllSuccsCompleted()) {
      w->completed = true;

      foreach(it, w->elms.begin(), w->elms.end()) {
        StaticRecord* rec = *it;

        foreach(sit, rec->sources.begin(), rec->sources.end()) {
          StateRecord* stateRecord = *sit;
          stateRecord->reterminate();
          reterminateSources.push_back(stateRecord);
        }
      }

      if (debug) {
        std::cout << "COMPLETED" << std::endl;
        std::cout << "\tpreds: " << std::endl;
      }

      unsigned scci = 0;

      foreach(it, w->preds.begin(), w->preds.end()) {
        StaticRecordSCC* pred = *it;

        if (debug) {
          std::cout << "scc#=" << (scci++) << " complete=" << pred->completed << std::endl;
          pred->print();
        }

        assert(pred);
        worklist.push_back(pred);
      }
    }
  }

  if (debug) {
    std::cout << "================================================" << std::endl;
  }

  //reterminate(reterminateSources);
}
/*
void StaticRecord::reterminate(std::vector<StateRecord*>& reterminateSources) {
  std::set<StateRecord*> visited;
  std::list<StateRecord*> worklist;

  foreach(it, reterminateSources.begin(), reterminateSources.end()) {
    StateRecord* rec = *it;
    worklist.push_back(rec);
    visited.insert(rec);
  }

  while (!worklist.empty()) {
    StateRecord* rec = worklist.front();
    worklist.pop_front();

    rec->clearReterminated();

    if (rec->parent && rec->parent->isTerminated()) {
      if (!visited.count(rec->parent) && rec->parent) {
        worklist.push_back(rec->parent);
        visited.insert(rec->parent);
      }
    }
  }


}*/

void StaticRecord::addControlSucc(StaticRecord * n) {
  if (((this->function->getNameStr() == "xalloc_die") && (n->function->getNameStr() == "__error")) ||
          ((this->function->getNameStr() == "close_stdout") && (n->function->getNameStr() == "__error"))) {
    return;
  }


  control_succs.insert(n);
  n->control_preds.insert(this);
}

StaticRecord::StaticRecord(KFunction* _kfunction, BasicBlock * _basicBlock, unsigned _index) :
kfunction(_kfunction), function(_kfunction->function), basicBlock(_basicBlock), index(_index),
covered(false), controlsExit(false), ipostdom(NULL),
iPostDomIsExit(false), iPostDomIsSuperExit(false), scc(0) {

}
///////////////////////////////////////////////////////////////////////////////

void StaticRecordManager::san(std::string& s) {
  std::replace(s.begin(), s.end(), '.', '_');
}

void StaticRecordManager::writeCFGraph(Function* function) {
  std::ofstream os;
  std::string name = function->getNameStr() + ".cfg.dot";
  os.open(name.c_str());

  os << "digraph {\n";

  foreach(itn, funrecs[function].begin(), funrecs[function].end()) {
    StaticRecord* n = *itn;

    foreach(itsucc, n->succs.begin(), n->succs.end()) {
      StaticRecord* s = *itsucc;

      std::string s1 = n->name();
      std::string s2 = s->name();
      san(s1);
      san(s2);
      os << "\t" << s1 << " -> " << s2 << ";\n";
    }
  }

  os << "}";

  os.flush();
  os.close();
}

void StaticRecordManager::findDeepestIncomplete(StaticRecordSCC* scc) {
  std::list<StaticRecordSCC*> worklist;
  worklist.push_back(scc);

  while (!worklist.empty()) {
    StaticRecordSCC* w = worklist.front();
    worklist.pop_front();

    if (!w->completed) {
      if (w->succs.empty()) {
        std::cout << "DEEPEST: ";
        w->print();
        return;
      } else {

        foreach(it, w->succs.begin(), w->succs.end()) {
          StaticRecordSCC* s = *it;
          worklist.push_back(s);
        }
      }
    }
  }
}

StaticRecordManager::StaticRecordManager(KModule * _kmodule) : kmodule(_kmodule) {
  std::map<BasicBlock*, StaticRecord*> first;
  std::map<BasicBlock*, StaticRecord*> last;

  foreach(it, kmodule->functions.begin(), kmodule->functions.end()) {
    KFunction* kf = *it;
    Function* f = kf->function;
    if (f->isDeclaration()) continue;

    foreach(bbit, f->begin(), f->end()) {
      BasicBlock* bb = &*bbit;
      StaticRecord* prevRec = NULL;
      StaticRecord* rec = NULL;

      unsigned reci = 0;
      unsigned i = kf->basicBlockEntry[bb];
      Instruction* inst = NULL;
      do {
        KInstruction* ki = kf->instructions[i];
        if (inst != NULL && isa<PHINode > (inst) && !isa<PHINode > (ki->inst)) {
          prevRec = rec;
          rec = NULL;
        }

        inst = ki->inst;

        if (!rec) {
          rec = new StaticRecord(kf, inst->getParent(), reci++);
          funrecs[f].push_back(rec);
          nodes.push_back(rec);
          if (prevRec) {
            prevRec->succs.insert(rec);
            rec->preds.insert(prevRec);
          } else {
            first[bb] = rec;
          }
        }

        assert(!ki->staticRecord);
        ki->staticRecord = rec;
        rec->insts.push_back(inst);

        if (isa<CallInst > (inst)) {
          prevRec = rec;
          rec = NULL;
        }

        i++;
      } while (!isa<TerminatorInst > (inst));

      assert(rec);
      last[bb] = rec;
    }

    entry[f] = first[&(f->front())];

    foreach(bbit, f->begin(), f->end()) {
      BasicBlock* bb = &*bbit;

      foreach(succit, succ_begin(bb), succ_end(bb)) {
        BasicBlock* succbb = *succit;
        last[bb]->succs.insert(first[succbb]);
        first[succbb]->preds.insert(last[bb]);
      }
    }

    // writeCFGraph(f);
  }
}

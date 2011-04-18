#include "ESEStats.h"
#include "llvm/Instruction.h"
#include "llvm/BasicBlock.h"
#include "llvm/Function.h"
#include "llvm/GlobalVariable.h"
#include "StaticRecord.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/util/ExprUtil.h"
#include "DependenceNode.h"
#include "StateRecordComparer.h"
#include "klee/ExecutionState.h"
#include "EquivalentStateEliminator.h"
#include "CallStringHasher.h"

#include "StateRecord.h"
#include "Memory.h"
#include "klee/Internal/Module/Cell.h"
#include "llvm/Support/CommandLine.h"
#include "Sugar.h"
#include "AddressSpace.h"
#include <list>
#include <vector>
#include <set>
#include <algorithm>

using namespace klee;
using namespace llvm;

cl::opt<std::string>
PrintRecord("print-record", cl::desc("Print record"));

cl::opt<bool>
PrintForks("print-forks", cl::init(false));

unsigned StateRecord::segmentCount = 0;

void StateRecord::recopyLiveReadsInto(StateRecord* rec) {

  foreach(it, liveReads.begin(), liveReads.end()) {
    DependenceNode* n1 = *it;
    if (rec->copymap.find(n1) == rec->copymap.end()) {
      std::cout << "COPYMAP" << std::endl;
      std::cout << "rec=" << staticRecord->function->getNameStr() << " " << staticRecord->name() << std::endl;
      n1->print(std::cout);
      exit(1);
    }

    assert(rec->copymap.find(n1) != rec->copymap.end());

    foreach(cit, rec->copymap[n1].begin(), rec->copymap[n1].end()) {
      DependenceNode* n2 = *cit;
      rec->liveReads.push_back(n2);
    }
  }

  if (!liveControls.empty()) {
    assert(rec->regularControl);
    rec->liveControls.insert(rec->regularControl);
  }
}

void StateRecord::copyLiveReadsInto(ExecutionState* state) {
  //copy this rec's live reads into state->rec

  foreach(it, liveReads.begin(), liveReads.end()) {
    DependenceNode* n1 = *it;

    if (StackWrite * sw1 = n1->toStackWrite()) {
      Cell& cell = state->getLocalCell(sw1->sfi, sw1->reg);
      assert(cell.stackWrite);
      state->rec->liveReads.push_back(cell.stackWrite);
      state->rec->copymap[n1].insert(cell.stackWrite);
    } else if (ConOffObjectWrite * cow1 = n1->toConOffObjectWrite()) {
      const ObjectState* os = state->getObjectState(cow1->mallocKey);
      std::map<unsigned, ConOffObjectWrite*>::const_iterator cow2it = os->conOffObjectWrites.find(cow1->offset);
      assert(cow2it != os->conOffObjectWrites.end());
      ConOffObjectWrite* cow2 = cow2it->second;
      //assert((cow2->offset + 1) * 8 <= os->size);
      state->rec->liveReads.push_back(cow2);
      state->rec->copymap[n1].insert(cow2);
    } else if (SymOffObjectWrite * sow1 = n1->toSymOffObjectWrite()) {
      const ObjectState* os = state->getObjectState(sow1->mallocKey);

      foreach(sowit, os->symOffObjectWrites.begin(), os->symOffObjectWrites.end()) {
        SymOffObjectWrite* sow2 = *sowit;
        state->rec->liveReads.push_back(sow2);
        state->rec->copymap[n1].insert(sow2);
      }
    } else if (SymOffArrayAlloc * a = n1->toSymOffArrayAlloc()) {
      const MallocKey& mallocKey = a->mallocKey;
      StateRecord* allocRec = state->mallocKeyAlloc[mallocKey];
      assert(allocRec);
      SymOffArrayAlloc* soaa = state->rec->symOffArrayAlloc(state, allocRec, mallocKey);
      state->rec->liveReads.push_back(soaa);
      state->rec->copymap[n1].insert(soaa);
    } else if (ConOffArrayAlloc * a = n1->toConOffArrayAlloc()) {
      const MallocKey& mallocKey = a->mallocKeyOffset.mallocKey;
      unsigned offset = a->mallocKeyOffset.offset;
      StateRecord* allocRec = state->mallocKeyAlloc[mallocKey];
      assert(allocRec);
      ConOffArrayAlloc* coaa = state->rec->conOffArrayAlloc(state, allocRec, mallocKey, offset);
      state->rec->liveReads.push_back(coaa);
      state->rec->copymap[n1].insert(coaa);
    } else {
      assert(false);
    }
  }

  if (!liveControls.empty()) {
    assert(state->rec->regularControl);
    state->rec->liveControls.insert(state->rec->regularControl);
  }


}

void StateRecord::setReterminated() {
  //TODO:: re-enabel assert in setReterminated
  //assert(!reterminated);
  reterminated = true;
}

void StateRecord::clearReterminated() {
  reterminated = false;
}

void StateRecord::setExecuted() {
  assert(!executed);
  executed = true;
}

void StateRecord::clear() {
  liveConstraints.clear();
  liveReads.clear();
  liveControls.clear();
}

StateRecordComparer* StateRecord::getComparer() {
  assert(comparer);
  return comparer;
}

void StateRecord::addToPrunedSet(StateRecord* rec) {
  prunedSet.insert(rec);
}

bool StateRecord::isExecuted() {
  return executed;
}

void StateRecord::execute() {
  assert(!executed);
  executed = true;
  state = NULL;
}

bool StateRecord::isPruned() {
  return pruned;
}

bool StateRecord::isTerminated() {
  return terminated;
}

ExecutionState* StateRecord::getState() {
  return state;
}

void StateRecord::clearState() {
  this->state = NULL;
}

void StateRecord::setPruned() {
  assert(!this->pruned);
  this->pruned = true;
}

void StateRecord::reterminate(unsigned depth) {
  std::cout << "RETERMINATE: " << depth << " " << this << staticRecord->function->getNameStr() << " " << staticRecord->name() << std::endl;
  StateRecord* rec = this;

  while (rec && rec->isTerminated()) {
    rec->clear();
    rec->terminate();
    assert(rec->comparer);
    rec->getComparer()->notifyReterminated(rec);

    foreach(it, rec->prunedSet.begin(), rec->prunedSet.end()) {
      StateRecord* p = *it;
      assert(p->children.empty());
      p->clear();
      rec->recopyLiveReadsInto(p);
      p->parent->reterminate(depth++);
    }

    rec = rec->parent;
  }
}

void StateRecord::terminate() {
  ESEStats::termRecCount++;

  std::set<DependenceNode*> frontier;
  std::list<DependenceNode*> worklist;

  bool childrenHaveLiveReads = false;

  foreach(it, children.begin(), children.end()) {
    StateRecord* c = *it;

    foreach(cit, c->liveReads.begin(), c->liveReads.end()) {
      DependenceNode* r = *cit;
      childrenHaveLiveReads = true;
      worklist.push_back(r);
      frontier.insert(r);
    }

    liveControls.insert(c->liveControls.begin(), c->liveControls.end());
  }

  if (branchRead) {
    assert(staticRecord->scc);
    if (!staticRecord->scc->completed || (childrenHaveLiveReads && isExitControl) || liveControls.count(branchRead)) {
      if (!staticRecord->scc->completed) {
        staticRecord->sources.insert(this);
      }

      liveControls.erase(branchRead);
      worklist.push_back(branchRead);
      frontier.insert(branchRead);
    }
  }

  bool includeCurrentRecordControls = false;
  while (!worklist.empty()) {
    DependenceNode* n2 = worklist.front();
    worklist.pop_front();

    if (n2->rec != this) continue;

    includeCurrentRecordControls = true;
    frontier.erase(n2);

    foreach(it, n2->preds.begin(), n2->preds.end()) {
      DependenceNode* n1 = *it;

      worklist.push_back(n1);
      frontier.insert(n1);
    }
  }

  if (includeCurrentRecordControls) {
    if (regularControl)
      liveControls.insert(regularControl);
  }

  foreach(it, frontier.begin(), frontier.end()) {
    DependenceNode* n = *it;
    liveReads.push_back(n);
  }

  terminated = true;

  foreach(it, children.begin(), children.end()) {
    StateRecord* rec = *it;
    assert(rec->terminated || rec->pruned);
  }

  getLiveConstraints(constraints, liveConstraints);
  //getLiveConstraints(como2cn, somo2cn, liveConstraints);

  if ((staticRecord->function->getNameStr() == "exit") || (staticRecord->function->getNameStr() == "__error")) {
    liveReads.clear();
  }


  if (staticRecord->function->getNameStr() == PrintRecord) {
    print();
  }

  size += liveConstraints.size() * (sizeof (ref<Expr>) + 16);
  size += liveReads.size() * (sizeof (unsigned) + 16);
  size += liveControls.size() * (sizeof (unsigned) + 16);

  /*if (liveControls.size() > 1) {
    std::cout << staticRecord->function->getNameStr() << " "  << staticRecord->name() << std::endl;
    foreach (it, liveControls.begin(), liveControls.end()) {
      DependenceNode* n = *it;
      std::cout << n->rec->staticRecord->name() << " ";
      n->print(std::cout);
      std::cout << std::endl;
    }
    exit(1);
  }*/
  /*
    if (staticRecord->function->getNameStr() == "strncmp") {

      if (staticRecord->isReturn()) {
        bool found = false;

        foreach(it, liveReads.begin(), liveReads.end()) {
          DependenceNode* d = *it;
          if (StackWrite * sw = d->toStackWrite()) {
            if (sw->kf->function->getNameStr() == "strncmp") {
              if (sw->reg == 22) {
                found = true;
              }
            }
          }
        }
        if (!found) {
          print();
          std::cout << "STACK" << std::endl;

          foreach(it, callers.begin(), callers.end()) {
            Instruction* ci = *it;
            std::cout << " " << ci->getParent()->getParent()->getNameStr() << " " << ci->getParent()->getNameStr() << std::endl;
          }

          printPathToExit();
          exit(1);
        }
      }
    }*/


  /*
  if (staticRecord->function->getNameStr() == "__base_main") {
    if (staticRecord->basicBlock->getNameStr() == "bb2") {
      if (!liveReads.empty()) {
   printPathToExit();
        exit(1);
      }
    }
  }*/
}

StateRecord::StateRecord(EquivalentStateEliminator* _elim, ExecutionState * _state, StateRecordComparer* _comparer) :
executed(false),
comparer(_comparer),
pruned(false),
isShared(false),
lochash(0),
valhash(0),
staticRecord(_state->pc->staticRecord),
branchRead(0),
inst(_state->pc->inst),
elim(_elim),
parent(NULL),
hash(CallStringHasher::hash(_state)),
kf(_state->stack.back().kf),
call(_state->stack.back().call),
sfi(_state->stack.size() - 1),
curinst(0),
constraints(_state->constraints),
/*como2cn(_state->como2cn), somo2cn(_state->somo2cn), */
terminated(false), regularControl(NULL), isExitControl(false),
state(_state), currentSegment(0), holder(NULL) {

  if ((kf->function->getNameStr().find("memcpy") != std::string::npos) ||
          (kf->function->getNameStr().find("memset") != std::string::npos) ||
          (kf->function->getNameStr().find("strlen") != std::string::npos) ||
          (kf->function->getNameStr().find("strcpy") != std::string::npos))
    inleafcount++;
  count++;

  for (unsigned i = 1; i < _state->stack.size(); i++) {
    StackFrame& sf = _state->stack[i];
    callers.push_back(sf.caller->inst);
  }
  /*
    foreach(it, como2cn.begin(), como2cn.end()) {
      const MallocKeyOffset& mk = it->first;
      const std::set<ref<Expr>, ExprLessThan>& s = it->second;

      size += sizeof (MallocKeyOffset);
      size += (sizeof (ref<Expr>) + 2) * s.size();
    }

    foreach(it, somo2cn.begin(), somo2cn.end()) {
      const MallocKey& mk = it->first;
      const std::set<ref<Expr>, ExprLessThan>& s = it->second;

      size += sizeof (MallocKey);
      size += (sizeof (ref<Expr>) + 2) * s.size();
    }*/

  size += constraints.size() * sizeof (ref<Expr>);
  size += callers.size() * sizeof (unsigned);
  size += sizeof (StateRecord);
  size += sizeof (unsigned) * writes.size();
  size += children.size() * (sizeof (unsigned) + 2);
}

static bool isConstant(const ObjectState* os) {
  const Value* v = os->getObject()->mallocKey.allocSite;
  if (!v) return false;

  if (const GlobalVariable * gv = dyn_cast<GlobalVariable > (v)) {
    if (gv->isConstant()) {
      return true;
    }
  }
  return false;
}

void StateRecord::conOffObjectRead(const ObjectState* os, unsigned offset) {
  if (isConstant(os)) return;

  std::map<unsigned, ConOffObjectWrite*>::const_iterator it = os->conOffObjectWrites.find(offset);
  //assert(it != os->conOffObjectWrites.end());
  if (it != os->conOffObjectWrites.end()) {
    ConOffObjectWrite* w = it->second;
    curreads.insert(w);
  }

  foreach(it, os->symOffObjectWrites.begin(), os->symOffObjectWrites.end()) {
    SymOffObjectWrite* w = *it;
    curreads.insert(w);
  }
}

void StateRecord::symOffObjectRead(const ObjectState* os) {
  if (isConstant(os)) return;

  foreach(it, os->conOffObjectWrites.begin(), os->conOffObjectWrites.end()) {
    ConOffObjectWrite* w = it->second;
    curreads.insert(w);
  }

  foreach(it, os->symOffObjectWrites.begin(), os->symOffObjectWrites.end()) {
    SymOffObjectWrite* w = *it;
    curreads.insert(w);
  }
}

void StateRecord::conOffObjectWrite(ObjectState* os, unsigned offset, ref<Expr> value) {
  if (isConstant(os)) return;

  ConOffObjectWrite* cowrite = new ConOffObjectWrite(this, os, offset, value);

  foreach(it, curreads.begin(), curreads.end()) {
    cowrite->addPred(*it);
  }
  os->conOffObjectWrites.erase(offset);
  os->conOffObjectWrites[offset] = cowrite;

  writes.insert(cowrite);
}

void StateRecord::symOffObjectWrite(ObjectState* os) {
  if (isConstant(os)) return;

  SymOffObjectWrite* sowrite = new SymOffObjectWrite(this, os);

  foreach(it, curreads.begin(), curreads.end()) {
    sowrite->addPred(*it);
  }
  os->symOffObjectWrites.push_back(sowrite);
  writes.insert(sowrite);
}

void StateRecord::stackRead(StackWrite* sw) {
  assert(sw->toStackWrite());
  if (isa<BranchInst > (curinst) || isa<SwitchInst > (curinst)) {
    branchRead = sw;
  }

  curreads.insert(sw);
}

ConOffArrayAlloc* StateRecord::conOffArrayAlloc(const ExecutionState* state, StateRecord* writer, const MallocKey& mallocKey, unsigned offset) {
  std::map<MallocKeyOffset, ConOffArrayAlloc*>::iterator it = state->conOffArrayAllocs.find(MallocKeyOffset(mallocKey, offset));
  ConOffArrayAlloc* a = NULL;
  if (it == state->conOffArrayAllocs.end()) {
    a = new ConOffArrayAlloc(writer, mallocKey, offset);
    state->conOffArrayAllocs.insert(std::make_pair(MallocKeyOffset(mallocKey, offset), a));
    writer->writes.insert(a);
  } else {
    a = it->second;
  }
  return a;
}

SymOffArrayAlloc* StateRecord::symOffArrayAlloc(const ExecutionState* state, StateRecord* writer, const MallocKey& mallocKey) {
  SymOffArrayAlloc* a = state->symOffArrayAlloc;
  if (!a) {
    a = new SymOffArrayAlloc(writer, mallocKey);
    state->symOffArrayAlloc = a;
    writer->writes.insert(a);
  }
  return a;
}

void StateRecord::arrayRead(const ExecutionState* state, ref<ReadExpr> e) {
  const Array* array = e->updates.root;
  const MallocKey& mallocKey = array->mallocKey;

  if (ConstantExpr * ce = dyn_cast<ConstantExpr > (e->index)) {
    unsigned offset = (uint8_t) ce->getZExtValue();
    curreads.insert(conOffArrayAlloc(state, array->rec, mallocKey, offset));
  } else {
    curreads.insert(symOffArrayAlloc(state, array->rec, mallocKey));
  }

}

StackWrite* StateRecord::stackWrite(KFunction* kf, unsigned call, unsigned sfi, unsigned reg, ref<Expr> value) {
  StackWrite* stackWrite = new StackWrite(this, kf, call, sfi, reg, value);

  foreach(it, curreads.begin(), curreads.end()) {
    stackWrite->addPred(*it);
  }
  writes.insert(stackWrite);
  return stackWrite;
}

void StateRecord::printCallString() {

  foreach(it, callers.begin(), callers.end()) {
    Instruction* inst = *it;
    std::cout << " " << *inst << std::endl;
  }
}



unsigned StateRecord::inleafcount = 0;
unsigned StateRecord::count = 0;
unsigned StateRecord::size = 0;

void StateRecord::printReads() {

  foreach(it, liveReads.begin(), liveReads.end()) {
    DependenceNode* n = *it;
    n->print(std::cout);
    std::cout << std::endl;
  }
}

void StateRecord::printControls() {

  foreach(it, liveControls.begin(), liveControls.end()) {
    DependenceNode* n = *it;
    n->print(std::cout);
    std::cout << std::endl;

  }

}

void StateRecord::printCurControls() {
  if (regularControl) {
    std::cout << " reg: ";
    regularControl->print(std::cout);
    std::cout << std::endl;
  }

}

StateRecord::~StateRecord() {

  foreach(it, writes.begin(), writes.end()) {
    DependenceNode* dn = *it;
    delete dn;
  }
}

void StateRecord::split(ExecutionState* s1, ExecutionState* s2) {
  ESEStats::forks[s1->prevPC->inst]++;
  if (PrintForks) std::cout << "FORK: " << s1->rec << " " << s1->prevPC->inst->getParent()->getParent()->getNameStr() << " " << *(s1->prevPC->inst) << std::endl;
  s1->rec->isShared = true;
  assert(s2->rec->isShared);
}

bool StateRecord::isEquiv(ExecutionState* esi2) {
  bool c;

  ESEStats::stackTimer.start();
  c = equivStack(esi2);
  ESEStats::stackTimer.stop();
  if (!c) {
    return false;
  }

  ESEStats::constraintTimer.start();
  c = equivConstraints(esi2);
  ESEStats::constraintTimer.stop();
  if (!c) {
    return false;
  }

  ESEStats::addressSpaceTimer.start();
  c = equivAddressSpace(esi2);
  ESEStats::addressSpaceTimer.stop();
  if (!c) {
    return false;
  }

  return true;
}

void StateRecord::getLiveConstraints(const ConstraintManager& constraints,
        std::vector<ref<Expr> >& result) {

  std::set<MallocKeyOffset> liveMallocKeyOffsets;
  std::set<MallocKey> liveMallocKeys;

  foreach(it, liveReads.begin(), liveReads.end()) {
    MallocKey* mallocKey = NULL;
    if (SymOffObjectWrite * swr = (*it)->toSymOffObjectWrite()) {
      mallocKey = &swr->mallocKey;
    } else if (SymOffArrayAlloc * aa = (*it)->toSymOffArrayAlloc()) {
      mallocKey = &aa->mallocKey;
    } else {
      continue;
    }

    liveMallocKeys.insert(*mallocKey);
  }

  foreach(it, liveReads.begin(), liveReads.end()) {
    MallocKey* mallocKey = NULL;
    unsigned offset;
    if (ConOffObjectWrite * swr = (*it)->toConOffObjectWrite()) {
      mallocKey = &(swr->mallocKey);
      offset = swr->offset;
    } else if (ConOffArrayAlloc * aa = (*it)->toConOffArrayAlloc()) {
      mallocKey = &aa->mallocKeyOffset.mallocKey;
      offset = aa->mallocKeyOffset.offset;
    } else {
      continue;
    }

    liveMallocKeyOffsets.insert(MallocKeyOffset(*mallocKey, offset));
  }

  getLiveConstraints(constraints, liveMallocKeyOffsets, liveMallocKeys, result);
}

void StateRecord::getLiveConstraints(const ConstraintManager& constraints,
        const std::set<MallocKeyOffset>& liveMallocKeyOffsets,
        const std::set<MallocKey>& liveMallocKey,
        std::vector<ref<Expr> >& result) {
  std::set<ref<Expr> > resultset;

  foreach(it, constraints.begin(), constraints.end()) {
    ref<Expr> constraint = *it;
    std::vector<ref<ReadExpr> > usedReadExprs;
    findReads(constraint, true, usedReadExprs);

    for (std::vector<ref<ReadExpr> >::iterator it = usedReadExprs.begin(); it != usedReadExprs.end(); ++it) {
      ref<ReadExpr> re = *it;
      if (ConstantExpr * ce = dyn_cast<ConstantExpr > (re->index)) {
        unsigned offset = (uint8_t) ce->getZExtValue();
        MallocKeyOffset mko(re->updates.root->mallocKey, offset);
        if (liveMallocKeyOffsets.count(mko)) {
          resultset.insert(constraint);
        }
      } else {
        if (liveMallocKey.count(re->updates.root->mallocKey)) {
          resultset.insert(constraint);
        }
      }
    }
  }

  foreach(it, resultset.begin(), resultset.end()) {
    ref<Expr> e = *it;
    result.push_back(e);
  }
}

bool StateRecord::equivConstraints(ExecutionState* es2) {
  std::vector<ref<Expr> > es2LiveConstraints;

  //getLiveConstraints(es2->como2cn, es2->somo2cn, es2LiveConstraints);
  getLiveConstraints(es2->constraints, es2LiveConstraints);

  if (liveConstraints.size() != es2LiveConstraints.size()) {
    return false;
  }

  return liveConstraints == es2LiveConstraints;
}

bool StateRecord::equivStack(ExecutionState* es2) {

  assert(terminated);

  if (inst != es2->pc->inst) {
    return false;
  }

  if (callers.size() != (es2->stack.size() - 1)) {
    return false;
  }

  for (unsigned i = 0; i < callers.size(); i++) {
    Instruction* call = callers[i];
    StackFrame* sf2 = &(es2->stack[i + 1]);

    if (call != sf2->caller->inst) {
      return false;
    }
  }

  foreach(it, liveReads.begin(), liveReads.end()) {
    StackWrite* rd = (*it)->toStackWrite();
    if (!rd) continue;

    ref<Expr> e1 = rd->value;
    ref<Expr> e2 = es2->getLocalCell(rd->sfi, rd->reg).value;

    if (!equals(e1, e2)) {
      return false;
    }
  }

  return true;
}

bool StateRecord::equivAddressSpace(ExecutionState * es2) {

  foreach(it, liveReads.begin(), liveReads.end()) {
    if (ConOffObjectWrite * cwr = (*it)->toConOffObjectWrite()) {
      ref<Expr> e1 = cwr->value;
      const ObjectState* o2 = es2->getObjectState(cwr->mallocKey);
      if (!o2) {
        //continue;
        return false;
      }

      ref<Expr> e2 = o2->read8(cwr->offset, NULL);

      if (!equals(e1, e2)) {
        return false;
      }
    } else if (SymOffObjectWrite * swr = (*it)->toSymOffObjectWrite()) {
      ObjectState* o1 = swr->objectStateCopy;
      const ObjectState* o2 = es2->getObjectState(swr->mallocKey);
      if (!o2) {
        //continue;
        return false;
      }

      assert(o1);
      assert(o2);
      if (!o1->equals(o2)) {
        return false;
      }
    }
  }

  return true;
}

void StateRecord::addChild(StateRecord* rec) {
  assert(!children.count(rec));
  assert(!terminated);
  assert(!rec->terminated);
  children.insert(rec);
}

bool StateRecord::equals(ref<Expr> exp1, ref<Expr> exp2) {
  if ((exp1.get() == NULL && exp2.get() != NULL) || (exp1.get() != NULL && exp2.get() == NULL)) {
    return false;
  }

  if (exp1.get() == NULL && exp2.get() == NULL) {

    return true;
  }

  return exp1->compare(*exp2.get()) == 0;
}

bool StateRecord::haveAllChildrenTerminatedOrPruned() {

  foreach(it, children.begin(), children.end()) {
    StateRecord* child = *it;
    if (!child->terminated && !child->pruned) {

      return false;
    }
  }

  return true;
}

void StateRecord::print() {
  std::cout << "===========================================" << std::endl;
  std::cout << staticRecord->function->getNameStr() << " " << staticRecord->basicBlock->getNameStr() << " " << staticRecord->name() << std::endl;
  std::cout << "LIVE READS: " << std::endl;

  foreach(it, liveReads.begin(), liveReads.end()) {
    DependenceNode* n = *it;
    n->print(std::cout);
    std::cout << std::endl;
  }
  std::cout << "LIVE CONTROLS: " << std::endl;

  foreach(it, liveControls.begin(), liveControls.end()) {
    DependenceNode* n = *it;
    n->print(std::cout);
    std::cout << std::endl;
  }

  if (regularControl) {
    std::cout << "REG CONTROL: " << std::endl;
    regularControl->print(std::cout);
    std::cout << std::endl;
  }

  if (isExitControl) {
    std::cout << "EXIT CONTROL" << std::endl;
  }

  if (isTerminated()) {
    std::cout << "TERMINATED" << std::endl;
  }

  if (isShared) {
    std::cout << "SHARED" << std::endl;
  }
}

void StateRecord::printPathToExit() {
  StateRecord* rec = this;
  while (rec) {
    std::cout << "CHILDREN: " << rec->children.size() << std::endl;
    rec->print();
    StateRecord* next = NULL;

    foreach(it, rec->children.begin(), rec->children.end()) {
      StateRecord* r = *it;
      if (!r->liveReads.empty()) {
        next = r;
        break;
      }
    }

    if (!next) {
      if (!rec->children.empty()) {
        next = *(rec->children.begin());
      } else {
        break;
      }
    }
    rec = next;
  }

  std::cout << "FINISH PATH" << std::endl;
}

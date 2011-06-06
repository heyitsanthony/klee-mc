//===-- ExecutionState.h ----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_EXECUTIONSTATE_H
#define KLEE_EXECUTIONSTATE_H

#include "klee/Constraints.h"
#include "klee/Expr.h"
#include "klee/util/ExprUtil.h"
#include "klee/Internal/ADT/TreeStream.h"
#include "klee/Internal/Module/Cell.h"
// FIXME: We do not want to be exposing these? :(
#include "../../lib/Core/AddressSpace.h"
#include "../../lib/Core/BranchTracker.h"
#include "../../lib/Core/StateRecord.h"
#include "klee/Internal/Module/KInstIterator.h"
#include "../../lib/Core/Memory.h"
#include "llvm/Function.h"
#include "Internal/Module/KInstruction.h"
#include "llvm/Instructions.h"

#include <map>
#include <set>
#include <vector>

namespace klee {
  class Array;
  class CallPathNode;
  class Cell;
  class KFunction;
  class KInstruction;
  class MemoryObject;
  class PTreeNode;
  class InstructionInfo;
  class ExecutionTraceEvent;
  class StateRecord;

std::ostream &operator<<(std::ostream &os, const MemoryMap &mm);

struct StackFrame {
  friend class ExecutionState;    
  unsigned call;
  KInstIterator caller;
  KFunction *kf;
  CallPathNode *callPathNode;

  std::vector<const MemoryObject*> allocas;
//private:
  Cell *locals;
public:
  /// Minimum distance to an uncovered instruction once the function
  /// returns. This is not a good place for this but is used to
  /// quickly compute the context sensitive minimum distance to an
  /// uncovered instruction. This value is updated by the StatsTracker
  /// periodically.
  unsigned minDistToUncoveredOnReturn;

  // For vararg functions: arguments not passed via parameter are
  // stored (packed tightly) in a local (alloca) memory object. This
  // is setup to match the way the front-end generates vaarg code (it
  // does not pass vaarg through as expected). VACopy is lowered inside
  // of intrinsic lowering.
  MemoryObject *varargs;

  StackFrame(KInstIterator caller, KFunction *kf);
  StackFrame(const StackFrame &s);
  ~StackFrame();
  StackFrame& operator=(const StackFrame &s);
};

// FIXME: Redo execution trace stuff to use a TreeStream, there is no
// need to keep this stuff in memory as far as I can tell.

// each state should have only one of these guys ...
class ExecutionTraceManager {
public:
  ExecutionTraceManager() : hasSeenUserMain(false) {}

  void addEvent(ExecutionTraceEvent* evt);
  void printAllEvents(std::ostream &os) const;

private:
  // have we seen a call to __user_main() yet?
  // don't bother tracing anything until we see this,
  // or else we'll get junky prologue shit
  bool hasSeenUserMain;

  // ugh C++ only supports polymorphic calls thru pointers
  //
  // WARNING: these are NEVER FREED, because they are shared
  // across different states (when states fork), so we have 
  // an *intentional* memory leak, but oh wellz ;)
  std::vector<ExecutionTraceEvent*> events;
};

/* Represents a memory array, its materialization, and ... */
class SymbolicArray
{
public:
  SymbolicArray(const MemoryObject* in_mo, const Array* in_array, ref<Expr> in_len)
  : mo(in_mo), array(in_array), len(in_len) {}
  virtual ~SymbolicArray() {}
  bool operator ==(const SymbolicArray& sa) const 
  { 
  	/* XXX ignore len for now XXX  XXX */
  	return (mo == sa.mo && array == sa.array);
  }
  const Array *getArray(void) const { return array; }
  const MemoryObject *getMemoryObject(void) const { return mo; }
private:
  const MemoryObject *mo;
  const Array *array;
  ref<Expr> len;
};

typedef std::set<ExecutionState*> ExeStateSet;
class ExecutionState {
public:
  typedef std::vector<StackFrame> stack_ty;    

private:
  // unsupported, use copy constructor
  ExecutionState &operator=(const ExecutionState&); 
  std::map< std::string, std::string > fnAliases;

public:
  mutable SymOffArrayAlloc* symOffArrayAlloc;
  StateRecord* prunepoint;
  bool pruned;
  StateRecord* rec; //for EquivalentStateEliminator
  std::list<StateRecord*> controlDependenceStack;
  mutable std::map<MallocKeyOffset, ConOffArrayAlloc*> conOffArrayAllocs;
  //como2cn_ty como2cn;
  //somo2cn_ty somo2cn;
  
  bool fakeState;
  // Are we currently underconstrained?  Hack: value is size to make fake
  // objects.
  unsigned underConstrained;
  unsigned depth;
  
  // pc - pointer to current instruction stream
  KInstIterator pc, prevPC;
  stack_ty stack;
  ConstraintManager constraints;
  mutable double queryCost;
  double weight;
  AddressSpace addressSpace;
  TreeOStream symPathOS;
  unsigned instsSinceCovNew;
  bool coveredNew;
  uint64_t lastChosen;

  // Number of malloc calls per callsite
  std::map<const llvm::Value*,unsigned> mallocIterations;

  // Ref counting for MemoryObject deallocation
  std::vector<ref<MemoryObject> > memObjects;

  // An ordered sequence of branches that this state took during execution thus
  // far:
  // XXX: ugh mutable for non-const copy constructor
  BranchTracker branchDecisionsSequence;

  // has true iff this state is a mere placeholder to be replaced by a real state
  bool isCompactForm;
  // for use with std::mem_fun[_ref] since they don't accept data members
  bool isCompactForm_f() const { return isCompactForm; }  
  bool isNonCompactForm_f() const { return !isCompactForm; }

  // used only if isCompactForm
  BranchTracker::iterator replayBranchIterator;

  // did this state start in replay mode?
  bool isReplay;

  // for printing execution traces when this state terminates
  ExecutionTraceManager exeTraceMgr;

  /// Disables forking, set by user code.
  bool forkDisabled;

  std::map<const std::string*, std::set<unsigned> > coveredLines;
  PTreeNode *ptreeNode;

  /// ordered list of symbolics: used to generate test cases. 
  //
  // FIXME: Move to a shared list structure (not critical).
  std::vector< SymbolicArray > symbolics;

  // Used by the checkpoint/rollback methods for fake objects.
  // FIXME: not freeing things on branch deletion.
  MemoryMap shadowObjects;

  unsigned incomingBBIndex;

  typedef std::map<MallocKey, const MemoryObject*> MallocKeyMap;
 
  MallocKeyMap mallocKeyMap;
  std::map<MallocKey, StateRecord*> mallocKeyAlloc;
 
  std::string getFnAlias(std::string fn);
  void addFnAlias(std::string old_fn, std::string new_fn);
  void removeFnAlias(std::string fn);


private:
  ExecutionState()
    : symOffArrayAlloc(0), prunepoint(0), pruned(false), rec(0), fakeState(false), underConstrained(0), coveredNew(false),
      lastChosen(0), isCompactForm(false), isReplay(false), ptreeNode(0) {
    replayBranchIterator = branchDecisionsSequence.begin();
  };

public:
  ExecutionState(KFunction *kf);

  // XXX total hack, just used to make a state so solver can
  // use on structure
  ExecutionState(const std::vector<ref<Expr> > &assumptions);

  ~ExecutionState();

  const ObjectState* getObjectState(const MallocKey& mk) {
      MallocKeyMap::iterator it = mallocKeyMap.find(mk);
      if (it == mallocKeyMap.end())
          return 0;
      const MemoryObject* mo = it->second;
      return addressSpace.findObject(mo);
  }
  
  ExecutionState *branch();
  ExecutionState *branchForReplay();
  ExecutionState *compact() const;
  ExecutionState *reconstitute(ExecutionState &initialStateCopy) const;

  void pushFrame(KInstIterator caller, KFunction *kf);
  void popFrame();

  void addSymbolic(
    const MemoryObject *mo, const Array *array, ref<Expr> len) {
    symbolics.push_back(SymbolicArray(mo, array, len));
  }

  void addConstraint(ref<Expr> constraint) {
  /*  if (rec) {
      std::vector<ref<ReadExpr> > usedReadExprs;

      findReads(constraint, true, usedReadExprs);

      for (std::vector<ref<ReadExpr> >::iterator it = usedReadExprs.begin();
              it != usedReadExprs.end(); ++it) {
        ref<ReadExpr> re = *it;
        if (ConstantExpr* ce = dyn_cast<ConstantExpr > (re->index)) {
          unsigned offset = (uint8_t) ce->getZExtValue();
          como2cn[MallocKeyOffset(re->updates.root->mallocKey, offset)].insert(constraint);
        }
        else {
          somo2cn[re->updates.root->mallocKey].insert(constraint);
        }
      }
    }*/
    
    constraints.addConstraint(constraint);
  }

  bool merge(const ExecutionState &b);

  void copy(ObjectState* os, const ObjectState* reallocFrom, unsigned count) {
    std::set<DependenceNode*> allReads;
    std::set<DependenceNode*> initialReads;
    if (rec) {
      allReads.insert(rec->curreads.begin(), rec->curreads.end());
      initialReads.insert(rec->curreads.begin(), rec->curreads.end());
    }

    for (unsigned i=0; i<count; i++) {
      if (rec) {
        rec->curreads.clear();
        rec->curreads.insert(initialReads.begin(), initialReads.end());
      }
      write(os, i, read8(reallocFrom, i));
      if (rec) {
        allReads.insert(rec->curreads.begin(), rec->curreads.end());
      }
    }

    if (rec) {
      rec->curreads.clear();
      rec->curreads.insert(allReads.begin(), allReads.end());
    }
  }

  ref<Expr>
  read(const ObjectState* object, ref<Expr> offset, Expr::Width width) const {    
    return object->read(offset, width, rec);
  }

  ref<Expr>
  read(const ObjectState* object, unsigned offset, Expr::Width width) const {    
    return object->read(offset, width, rec);
  }

  ref<Expr> read8(const ObjectState* object, unsigned offset) const {    
    return object->read8(offset, rec);
  }        

  void write(ObjectState* object, unsigned offset, ref<Expr> value) {
    object->write(offset, value, rec);
  }

  void write(ObjectState* object, ref<Expr> offset, ref<Expr> value) {    
    object->write(offset, value, rec);
    if (rec) {
      if (object->wasSymOffObjectWrite) {
        object->wasSymOffObjectWrite = false;
        rec->symOffObjectWrite(object);        
      }
    }
  }

  void write8(ObjectState* object, unsigned offset, uint8_t value) {
    object->write8(offset, value, rec);
  }

  void writeLocalCell(unsigned sfi, unsigned i, ref<Expr> value) {        
    assert(sfi < stack.size());
    const StackFrame& sf = stack[sfi];
    KFunction* kf = sf.kf;
    assert(i < kf->numRegisters);
    if (rec) {        
        sf.locals[i].stackWrite = rec->stackWrite(kf, sf.call, sfi, i, value);
    }
    sf.locals[i].value = value;
  }

  Cell& getLocalCell(unsigned sfi, unsigned i) const {
      /*if (sfi >= stack.size()) {
          std::cout << "sfi=" << sfi << " i=" <<  i << std::endl;
          for (unsigned i = 0; i < stack.size(); i++) {
              std::cout << " " << stack[i].kf->function->getNameStr() << std::endl;

          }
          exit(1);
      }*/
    assert(sfi < stack.size());
    const StackFrame& sf = stack[sfi];
    assert(i < sf.kf->numRegisters);
    return sf.locals[i];
  }

  Cell& readLocalCell(unsigned sfi, unsigned i) const
  {
    assert(sfi < stack.size());
    const StackFrame& sf = stack[sfi];
    KFunction* kf = sf.kf;
    assert(i < kf->numRegisters);
    if (!rec) return sf.locals[i];

    ref<Expr> e = sf.locals[i].value;
    StackWrite* sw = sf.locals[i].stackWrite;
    rec->stackRead(sw);

    if (isa<ConstantExpr > (e )) return sf.locals[i];
    std::vector<ref<ReadExpr> > usedReadExprs;

    findReads(e, true, usedReadExprs);

    for (std::vector<ref<ReadExpr> >::iterator it = usedReadExprs.begin();
          it != usedReadExprs.end(); ++it) {
      ref<ReadExpr> re = *it;
      rec->arrayRead(this, re);
    }

    return sf.locals[i];
  }

  void bindObject(const MemoryObject *mo, ObjectState *os) {
    if (rec) {      
      if (mo->mallocKey.allocSite) {
        assert((mallocKeyMap.find(mo->mallocKey) == mallocKeyMap.end()) || (mallocKeyMap[mo->mallocKey] == mo));
        mallocKeyMap[mo->mallocKey] = mo;
      }

      //StateRecord* existing = mallocKeyAlloc[mo->mallocKey];
      
      //assert(!existing);
        mallocKeyAlloc[mo->mallocKey] = os->allocRec;      
    }
    addressSpace.bindObject(mo, os);
  }
};


// for producing abbreviated execution traces to help visualize
// paths and diagnose bugs

class ExecutionTraceEvent {
public:
  // the location of the instruction:
  std::string file;
  unsigned line;
  std::string funcName;
  unsigned stackDepth;

  unsigned consecutiveCount; // init to 1, increase for CONSECUTIVE
                             // repetitions of the SAME event

  ExecutionTraceEvent()
    : file("global"), line(0), funcName("global_def"),
      consecutiveCount(1) {}

  ExecutionTraceEvent(ExecutionState& state, KInstruction* ki);

  virtual ~ExecutionTraceEvent() {}

  void print(std::ostream &os) const;

  // return true if it shouldn't be added to ExecutionTraceManager
  //
  virtual bool ignoreMe() const;

private:
  virtual void printDetails(std::ostream &os) const = 0;
};


class FunctionCallTraceEvent : public ExecutionTraceEvent {
public:
  std::string calleeFuncName;

  FunctionCallTraceEvent(ExecutionState& state, KInstruction* ki,
                         const std::string& _calleeFuncName)
    : ExecutionTraceEvent(state, ki), calleeFuncName(_calleeFuncName) {}

private:
  virtual void printDetails(std::ostream &os) const {
    os << "CALL " << calleeFuncName;
  }

};

class FunctionReturnTraceEvent : public ExecutionTraceEvent {
public:
  FunctionReturnTraceEvent(ExecutionState& state, KInstruction* ki)
    : ExecutionTraceEvent(state, ki) {}

private:
  virtual void printDetails(std::ostream &os) const {
    os << "RETURN";
  }
};

class BranchTraceEvent : public ExecutionTraceEvent {
public:
  bool trueTaken;         // which side taken?
  bool canForkGoBothWays;

  BranchTraceEvent(ExecutionState& state, KInstruction* ki,
                   bool _trueTaken, bool _isTwoWay)
    : ExecutionTraceEvent(state, ki),
      trueTaken(_trueTaken),
      canForkGoBothWays(_isTwoWay) {}

private:
  virtual void printDetails(std::ostream &os) const;
};

}

#endif

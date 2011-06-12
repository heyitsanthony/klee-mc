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
#include "klee/Internal/Module/KInstIterator.h"
#include "../../lib/Core/Memory.h"
#include "llvm/Function.h"
#include "Internal/Module/KInstruction.h"
#include "llvm/Instructions.h"

#include <map>
#include <set>
#include <vector>

#include "klee/StackFrame.h"


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

std::ostream &operator<<(std::ostream &os, const MemoryMap &mm);

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
class ExecutionState
{
public:
  typedef std::vector<StackFrame> stack_ty;    

private:
  // unsupported, use copy constructor
  ExecutionState &operator=(const ExecutionState&); 
  std::map< std::string, std::string > fnAliases;

public:
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

  std::string getFnAlias(std::string fn);
  void addFnAlias(std::string old_fn, std::string new_fn);
  void removeFnAlias(std::string fn);

  KInstIterator getCaller(void) const;
  void dumpStack(std::ostream &os);

private:
  ExecutionState()
    : fakeState(false),
      underConstrained(0),
      coveredNew(false),
      lastChosen(0),
      isCompactForm(false),
      isReplay(false),
      ptreeNode(0)
  {
    replayBranchIterator = branchDecisionsSequence.begin();
  }

public:
  ExecutionState(KFunction *kf);

  // XXX total hack, just used to make a state so solver can
  // use on structure
  ExecutionState(const std::vector<ref<Expr> > &assumptions);

  ~ExecutionState();

  ExecutionState *branch();
  ExecutionState *branchForReplay();
  ExecutionState *compact() const;
  ExecutionState *reconstitute(ExecutionState &initialStateCopy) const;

  void pushFrame(KInstIterator caller, KFunction *kf);
  void popFrame();

  void addSymbolic(
    const MemoryObject *mo, const Array *array, ref<Expr> len)
  {
    symbolics.push_back(SymbolicArray(mo, array, len));
  }

  void addConstraint(ref<Expr> constraint);
  bool merge(const ExecutionState &b);

  void copy(ObjectState* os, const ObjectState* reallocFrom, unsigned count);

  ref<Expr>
  read(const ObjectState* object, ref<Expr> offset, Expr::Width width) const {    
    return object->read(offset, width);
  }

  ref<Expr>
  read(const ObjectState* object, unsigned offset, Expr::Width width) const {    
    return object->read(offset, width);
  }

  ref<Expr> read8(const ObjectState* object, unsigned offset) const {    
    return object->read8(offset);
  }        

  void write(ObjectState* object, unsigned offset, ref<Expr> value) {
    object->write(offset, value);
  }

  void write(ObjectState* object, ref<Expr> offset, ref<Expr> value);

  void write8(ObjectState* object, unsigned offset, uint8_t value) {
    object->write8(offset, value);
  }

  void write64(ObjectState* object, unsigned offset, uint64_t value);

  void writeLocalCell(unsigned sfi, unsigned i, ref<Expr> value);

  Cell& getLocalCell(unsigned sfi, unsigned i) const;
  Cell& readLocalCell(unsigned sfi, unsigned i) const;

  void bindObject(const MemoryObject *mo, ObjectState *os);
};


// for producing abbreviated execution traces to help visualize
// paths and diagnose bugs

class ExecutionTraceEvent
{
public:
  // the location of the instruction:
  std::string file;
  unsigned line;
  std::string funcName;
  unsigned stackDepth;

  unsigned consecutiveCount; // init to 1, increase for CONSECUTIVE
                             // repetitions of the SAME event

  enum Kind {
    Invalid = -1,
    FunctionCall = 0,
    FunctionReturn,
    Branch
  };

  ExecutionTraceEvent()
    : file("global"), line(0), funcName("global_def"),
      consecutiveCount(1) {}

  ExecutionTraceEvent(ExecutionState& state, KInstruction* ki);

  virtual ~ExecutionTraceEvent() {}

  virtual Kind getKind(void) const = 0;

  void print(std::ostream &os) const;

  // return true if it shouldn't be added to ExecutionTraceManager
  //
  virtual bool ignoreMe() const;

  static bool classof(const ExecutionTraceEvent* ) { return true; }

private:
  virtual void printDetails(std::ostream &os) const = 0;
};


class FunctionCallTraceEvent : public ExecutionTraceEvent {
public:
  std::string calleeFuncName;

  FunctionCallTraceEvent(ExecutionState& state, KInstruction* ki,
                         const std::string& _calleeFuncName)
    : ExecutionTraceEvent(state, ki), calleeFuncName(_calleeFuncName) {}

  Kind getKind() const { return FunctionCall; }

  static bool classof(const ExecutionTraceEvent* e) {
  	return e->getKind() == FunctionCall;
  }

  static bool classof(const FunctionCallTraceEvent*) { return true; }

private:
  virtual void printDetails(std::ostream &os) const {
    os << "CALL " << calleeFuncName;
  }

};

class FunctionReturnTraceEvent : public ExecutionTraceEvent {
public:
  FunctionReturnTraceEvent(ExecutionState& state, KInstruction* ki)
    : ExecutionTraceEvent(state, ki) {}

  Kind getKind() const { return FunctionReturn; }

  static bool classof(const ExecutionTraceEvent *E) {
    return E->getKind() == FunctionReturn;
  }
  static bool classof(const FunctionReturnTraceEvent *) { return true; }

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

  Kind getKind() const { return Branch; }

  static bool classof(const BranchTraceEvent *) { return true; }

  static bool classof(const ExecutionTraceEvent *E) {
    return E->getKind() == Branch;
  }

private:
  virtual void printDetails(std::ostream &os) const;
};

}

#endif

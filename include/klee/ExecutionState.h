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
#include "klee/Interpreter.h"
#include "../../lib/Core/AddressSpace.h"
#include "../../lib/Core/BranchTracker.h"
#include "../../lib/Core/ExecutionTrace.h"
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

/* Represents a memory array, its materialization, and ... */
class SymbolicArray
{
public:
  SymbolicArray(
	MemoryObject* in_mo,
	Array* in_array,
	ref<Expr> in_len)
  : mo(in_mo), array(in_array), len(in_len) {}
  virtual ~SymbolicArray() {}
  bool operator ==(const SymbolicArray& sa) const
  {
  	/* XXX ignore len for now XXX  XXX */
	return (mo.get() == sa.mo.get() && array.get() == sa.array.get());
  }
  const Array *getArray(void) const { return array.get(); }
  const MemoryObject *getMemoryObject(void) const { return mo.get(); }
private:
  ref<MemoryObject>	mo;
  ref<Array>		array;
  ref<Expr>		len;
};

std::ostream &operator<<(std::ostream &os, const MemoryMap &mm);

// FIXME: Redo execution trace stuff to use a TreeStream, there is no
// need to keep this stuff in memory as far as I can tell.

typedef std::set<ExecutionState*> ExeStateSet;

typedef std::vector <std::vector<unsigned char> > RegLog;

class ExecutionState
{
public:
  typedef std::vector<StackFrame> stack_ty;

private:
  // unsupported, use copy constructor
  ExecutionState &operator=(const ExecutionState&);
  std::map< std::string, std::string > fnAliases;

  // An ordered sequence of branches that this state took during execution thus
  // far:
  // XXX: ugh mutable for non-const copy constructor
  BranchTracker branchDecisionsSequence;
  // used only if isCompactForm
  BranchTracker::iterator replayBranchIterator;

  unsigned incomingBBIndex;

  /// ordered list of symbolics: used to generate test cases.
  //
  // FIXME: Move to a shared list structure (not critical).
  std::vector< SymbolicArray > symbolics;

  RegLog		reg_log;
  RegLog		sc_log;
  MemoryObject		*reg_mo;
public:
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

  // has true iff this state is a mere placeholder to be replaced by a real state
  bool isCompactForm;
  // for use with std::mem_fun[_ref] since they don't accept data members
  bool isCompactForm_f() const { return isCompactForm; }
  bool isNonCompactForm_f() const { return !isCompactForm; }

  // did this state start in replay mode?
  bool isReplay;

  // for printing execution traces when this state terminates
  ExecutionTraceManager exeTraceMgr;

  /// Disables forking, set by user code.
  bool forkDisabled;

  std::map<const std::string*, std::set<unsigned> > coveredLines;
  PTreeNode *ptreeNode;

  std::string getFnAlias(std::string fn);
  void addFnAlias(std::string old_fn, std::string new_fn);
  void removeFnAlias(std::string fn);

  KInstIterator getCaller(void) const;
  void dumpStack(std::ostream &os);

private:
  ExecutionState()
    : underConstrained(0),
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

  static ExecutionState* createReplay(
	ExecutionState& initialState,
	const ReplayPathType& replayPath);

  ExecutionState *branch();
  ExecutionState *branchForReplay();
  ExecutionState *compact() const;
  ExecutionState *reconstitute(ExecutionState &initialStateCopy) const;

  void pushFrame(KInstIterator caller, KFunction *kf);
  void popFrame();

  void addSymbolic(MemoryObject *mo, Array *array, ref<Expr> len)
  {
    symbolics.push_back(SymbolicArray(mo, array, len));
  }

  bool addConstraint(ref<Expr> constraint);
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
  void unbindObject(const MemoryObject* mo);

  ObjectState* bindMemObj(const MemoryObject *mo, const Array *array = 0);
  ObjectState *bindStackMemObj(
    const MemoryObject *mo,
    const Array *array = 0);

  void transferToBasicBlock(llvm::BasicBlock* dst, llvm::BasicBlock* src);
  void bindLocal(KInstruction *target, ref<Expr> value);
  void bindArgument(
  	KFunction *kf, unsigned index, ref<Expr> value);

  KFunction* getCurrentKFunc() const;

  void trackBranch(int condIndex, int asmLine);
  bool isReplayDone(void) const;
  bool pushHeapRef(HeapObject* heapObj) {
	return branchDecisionsSequence.push_heap_ref(heapObj);
  }
  unsigned stepReplay(void);

  BranchTracker::iterator branchesBegin(void) const
  { return branchDecisionsSequence.begin(); }

  BranchTracker::iterator branchesEnd(void) const
  { return branchDecisionsSequence.end(); }

  unsigned getPHISlot(void) const { return incomingBBIndex * 2; }

  std::vector< SymbolicArray >::const_iterator symbolicsBegin(void) const
  {
  	return symbolics.begin();
  }

  std::vector< SymbolicArray >::const_iterator symbolicsEnd(void) const
  {
  	return symbolics.end();
  }

  void recordRegisters(const void* regs, int sz);
  RegLog::const_iterator regsBegin(void) const { return reg_log.begin(); }
  RegLog::const_iterator regsEnd(void) const { return reg_log.end(); }

  MemoryObject* setRegCtx(MemoryObject* mo)
  {
	MemoryObject	*old_mo;
	old_mo = reg_mo;
	reg_mo = mo;
	return old_mo;
  }

  MemoryObject* getRegCtx(void) const { return reg_mo; }
};

}

#endif

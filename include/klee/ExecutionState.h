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

#include "klee/ExeStateBuilder.h"
#include "klee/StackFrame.h"


namespace klee
{
class Array;
class CallPathNode;
class Cell;
class KFunction;
class KInstruction;
class MemoryObject;
class PTreeNode;
class InstructionInfo;
class MemoryManager;

/* Represents a memory array, its materialization, and ... */
class SymbolicArray
{
public:
	SymbolicArray(MemoryObject* in_mo, Array* in_array)
	: mo(in_mo), array(in_array) {}
	virtual ~SymbolicArray() {}
	bool operator ==(const SymbolicArray& sa) const
	{
		return (mo.get() == sa.mo.get() &&
			array.get() == sa.array.get());
	}
	const Array *getArray(void) const { return array.get(); }
	const MemoryObject *getMemoryObject(void) const { return mo.get(); }
private:
	ref<MemoryObject>	mo;
	ref<Array>		array;
};

std::ostream &operator<<(std::ostream &os, const MemoryMap &mm);

// FIXME: Redo execution trace stuff to use a TreeStream, there is no
// need to keep this stuff in memory as far as I can tell.

typedef std::set<ExecutionState*> ExeStateSet;

#define BaseExeStateBuilder	DefaultExeStateBuilder<ExecutionState>

class ExecutionState
{
friend class BaseExeStateBuilder;
private:
	static MemoryManager* mm;
	unsigned int num_allocs;

	// unsupported, use copy constructor
	ExecutionState &operator=(const ExecutionState&);
	std::map< std::string, std::string > fnAliases;

	// An ordered sequence of branches this state took thus far:
	// XXX: ugh mutable for non-const copy constructor
	BranchTracker branchDecisionsSequence;
	// used only if isCompactForm
	BranchTracker::iterator replayBranchIterator;

	unsigned incomingBBIndex;

	/// ordered list of symbolics: used to generate test cases.
	//
	// FIXME: Move to a shared list structure (not critical).
	std::vector< SymbolicArray > symbolics;
	typedef std::map<const Array*, const MemoryObject*> arr2sym_map;
	arr2sym_map	arr2sym;

public:
	typedef std::vector<StackFrame> stack_ty;

	// Are we currently underconstrained?  Hack: value is size to make fake
	// objects.
	unsigned underConstrained;
	unsigned depth;

	// pc - pointer to current instruction stream
	KInstIterator		pc, prevPC;
	stack_ty		stack;
	ConstraintManager	constraints;
	mutable double		queryCost;
	double			weight;
	AddressSpace		addressSpace;
	TreeOStream		symPathOS;
	unsigned		instsSinceCovNew;
	bool			coveredNew;
	uint64_t		lastChosen;

	// Number of malloc calls per callsite
	std::map<const llvm::Value*,unsigned> mallocIterations;

	// Ref counting for MemoryObject deallocation
	std::vector<ref<MemoryObject> > memObjects;

	// true iff this state is a mere placeholder
	// to be replaced by a real state
	bool isCompactForm;

	// for use with std::mem_fun[_ref] since they don't accept data members
	bool isCompactForm_f() const { return isCompactForm; }
	bool isNonCompactForm_f() const { return !isCompactForm; }

	unsigned int getNumAllocs(void) const { return num_allocs; }

	// did this state start in replay mode?
	bool isReplay;

	// for printing execution traces when this state terminates
	ExecutionTraceManager exeTraceMgr;

	/// Disables forking, set by user code.
	bool forkDisabled;

	std::map<const std::string*, std::set<unsigned> > coveredLines;
	PTreeNode *ptreeNode;
protected:
	ExecutionState();
	ExecutionState(KFunction *kf);
	// XXX total hack, just used to make a state so solver can
	// use on structure
	ExecutionState(const std::vector<ref<Expr> > &assumptions);
	void compact(ExecutionState* es) const;

public:
	static void setMemoryManager(MemoryManager* in_mm) { mm = in_mm; }
	ExecutionState* copy(void) const;

	virtual ExecutionState* copy(const ExecutionState* es) const
	{ return new ExecutionState(*es); }

	virtual ~ExecutionState();

	static ExecutionState* createReplay(
		ExecutionState& initialState,
		const ReplayPathType& replayPath);

	ExecutionState *branch();
	ExecutionState *branchForReplay();
	ExecutionState *compact() const;
	ExecutionState *reconstitute(ExecutionState &initialStateCopy) const;


	std::string getFnAlias(std::string fn);
	void addFnAlias(std::string old_fn, std::string new_fn);
	void removeFnAlias(std::string fn);

	KInstIterator getCaller(void) const;
	void dumpStack(std::ostream &os);

	KFunction* getCurrentKF(void) const { return (stack.back()).kf; }


	void pushFrame(KInstIterator caller, KFunction *kf);
	void popFrame();

	void addSymbolic(MemoryObject *mo, Array *array)
	{
		symbolics.push_back(SymbolicArray(mo, array));
		arr2sym[array] = mo;
	}

	const MemoryObject* findMemoryObject(const Array* a) const
	{
		arr2sym_map::const_iterator	it(arr2sym.find(a));
		return (it == arr2sym.end()) ? NULL : (*it).second;
	}


	ObjectState* allocate(
		uint64_t size, bool isLocal, bool isGlobal,
		const llvm::Value *allocSite);

	std::vector<ObjectState*> allocateAlignedChopped(
		uint64_t size, unsigned pow2,
		const llvm::Value *allocSite);

	ObjectState *allocateFixed(
		uint64_t address, uint64_t size,
		const llvm::Value *allocSite);

	ObjectState *allocateAt(
		uint64_t address, uint64_t size,
		const llvm::Value *allocSite);

	virtual void bindObject(const MemoryObject *mo, ObjectState *os);
	virtual void unbindObject(const MemoryObject* mo);

	ObjectState* bindMemObj(const MemoryObject *mo, const Array *array = 0);
	ObjectState *bindStackMemObj(const MemoryObject *mo, const Array *array = 0);



  bool addConstraint(ref<Expr> constraint);
  bool merge(const ExecutionState &b);

  void copy(ObjectState* os, const ObjectState* reallocFrom, unsigned count);

  ref<Expr>
  read(const ObjectState* obj, ref<Expr> offset, Expr::Width w) const
  { return obj->read(offset, w); }

  ref<Expr>
  read(const ObjectState* obj, unsigned offset, Expr::Width w) const
  { return obj->read(offset, w); }

  ref<Expr> read8(const ObjectState* obj, unsigned offset) const
  { return obj->read8(offset); }

  void write(ObjectState* obj, unsigned offset, ref<Expr> value)
  { obj->write(offset, value); }

  void write(ObjectState* obj, ref<Expr> offset, ref<Expr> value)
  { obj->write(offset, value); }

  void write8(ObjectState* obj, unsigned offset, uint8_t value)
  { obj->write8(offset, value); }

  void write64(ObjectState* obj, unsigned offset, uint64_t value);

  void writeLocalCell(unsigned sfi, unsigned i, ref<Expr> value);

  Cell& getLocalCell(unsigned sfi, unsigned i) const;
  Cell& readLocalCell(unsigned sfi, unsigned i) const;

  void transferToBasicBlock(llvm::BasicBlock* dst, llvm::BasicBlock* src);
  void bindLocal(KInstruction *target, ref<Expr> value);
  void bindArgument(KFunction *kf, unsigned index, ref<Expr> value);

  KFunction* getCurrentKFunc() const;

  void trackBranch(int condIndex, int asmLine);
  bool isReplayDone(void) const;
  bool pushHeapRef(HeapObject* heapObj)
  { return branchDecisionsSequence.push_heap_ref(heapObj); }

  unsigned stepReplay(void);

  BranchTracker::iterator branchesBegin(void) const
  { return branchDecisionsSequence.begin(); }

  BranchTracker::iterator branchesEnd(void) const
  { return branchDecisionsSequence.end(); }

  unsigned getPHISlot(void) const { return incomingBBIndex * 2; }

  std::vector< SymbolicArray >::const_iterator symbolicsBegin(void) const
  { return symbolics.begin(); }

  std::vector< SymbolicArray >::const_iterator symbolicsEnd(void) const
  { return symbolics.end(); }

  unsigned int getNumSymbolics(void) const { return symbolics.size(); }
};

}

#endif

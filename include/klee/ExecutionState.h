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
#include "klee/Internal/Module/Cell.h"
#include "../../lib/Core/AddressSpace.h"
#include "../../lib/Core/BranchTracker.h"
#include "../../lib/Core/ExecutionTrace.h"
#include "klee/Internal/Module/KInstIterator.h"
#include "../../lib/Core/Memory.h"

#include <map>
#include <set>
#include <vector>

#include "klee/ExeStateBuilder.h"
#include "klee/StackFrame.h"

namespace klee
{
class Array;
class Assignment;
class CallPathNode;
class Cell;
class KFunction;
class MemoryObject;
class PTreeNode;
struct InstructionInfo;
class MemoryManager;
class KInstruction;

/* Represents a memory array, its materialization, and ... */

/* basically for ref counting */
class ConcreteArray
{
public:
	ConcreteArray(const std::vector<uint8_t>& _v)
	: refCount(0), v(_v) {}
	~ConcreteArray() {}
	const std::vector<uint8_t>& getValues(void) const { return v; }

	mutable unsigned refCount;
private:
	std::vector<uint8_t> v;
};

class SymbolicArray
{
public:
	SymbolicArray(MemoryObject* in_mo, Array* in_array)
	: mo(in_mo), array(in_array), concretization(0) {}
	virtual ~SymbolicArray() {}
	bool operator ==(const SymbolicArray& sa) const
	{
		return (mo.get() == sa.mo.get() &&
			array.get() == sa.array.get());
	}
	const Array *getArray(void) const { return array.get(); }
	const ref<Array> getArrayRef(void) const { return array; }
	const MemoryObject *getMemoryObject(void) const { return mo.get(); }
	const std::vector<uint8_t>* getConcretization(void) const
	{
		if (concretization.isNull())
			return NULL;
		return &concretization->getValues();
	}

	void setConcretization(const std::vector<uint8_t>& v)
	{
		assert (concretization.isNull());
		concretization = new ConcreteArray(v);
	}
private:
	ref<MemoryObject>	mo;
	ref<Array>		array;
	ref<ConcreteArray>	concretization;
};

std::ostream &operator<<(std::ostream &os, const MemoryMap &mm);

// FIXME: Redo execution trace stuff to use a TreeStream, there is no
// need to keep this stuff in memory as far as I can tell.

typedef std::set<ExecutionState*> ExeStateSet;

#define BaseExeStateBuilder	DefaultExeStateBuilder<ExecutionState>
#define ES_CANARY_VALUE	0x11667744

class ExecutionState
{
friend class BaseExeStateBuilder;
private:

	// unsupported, use copy constructor
	ExecutionState &operator=(const ExecutionState&);

public:
	bool checkCanary(void) const { return canary == ES_CANARY_VALUE; }
	typedef std::vector<StackFrame> stack_ty;
	typedef stack_ty::iterator	stack_iter_ty;

	unsigned		depth;
	double			weight;

	// pc - pointer to current instruction stream
	KInstIterator		pc, prevPC;
	stack_ty		stack;
	ConstraintManager	constraints;
	mutable double		queryCost;
	AddressSpace		addressSpace;
	TreeOStream		symPathOS;
	unsigned		instsSinceCovNew;
	uint64_t		lastGlobalInstCount; // last stats::instructions
	uint64_t		totalInsts;
	unsigned		concretizeCount;
	ref<Expr>		prevForkCond;	// last condition to cause fork

	// Number of malloc calls per callsite
	std::map<const llvm::Value*,unsigned> mallocIterations;

	// Ref counting for MemoryObject deallocation
	std::list<ref<MemoryObject> > memObjects;

	bool			coveredNew;
	bool			isReplay;	/* started in replay mode? */
	ExecutionTraceManager	exeTraceMgr;	/* prints traces on exit */

	bool forkDisabled;	/* Disables forking, set by user code. */

	std::map<const std::string*, std::set<unsigned> > coveredLines;
	PTreeNode *ptreeNode;


	// for use with std::mem_fun[_ref] since they don't accept data members
	bool isCompact() const { return isCompactForm; }
	bool isNonCompact() const { return !isCompactForm; }

	unsigned int getNumAllocs(void) const { return num_allocs; }
	void setFreshBranch(void) { onFreshBranch = true; }
	void setOldBranch(void) { onFreshBranch = false; }
	bool isOnFreshBranch(void) const { return onFreshBranch; }
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

	ExecutionState *branch(bool forReplay = false);
	ExecutionState *compact() const;
	ExecutionState *reconstitute(ExecutionState &initialStateCopy) const;

	std::string getFnAlias(std::string fn);
	void addFnAlias(std::string old_fn, std::string new_fn);
	void removeFnAlias(std::string fn);

	KInstIterator getCaller(void) const;
	void dumpStack(std::ostream &os) const;
	virtual unsigned getStackDepth(void) const;
	void printConstraints(std::ostream& os) const;


	KFunction* getCurrentKFunc(void) const { return (stack.back()).kf; }

	void pushFrame(KInstIterator caller, KFunction *kf);
	void popFrame();
	void xferFrame(KFunction *kf);

	void addSymbolic(MemoryObject *mo, Array *array);

	const MemoryObject* findMemoryObject(const Array* a) const
	{
		arr2sym_map::const_iterator	it(arr2sym.find(a));
		return (it == arr2sym.end()) ? NULL : (*it).second;
	}


	ObjectPair allocate(
		uint64_t size, bool isLocal, bool isGlobal,
		const llvm::Value *allocSite);

	ObjectPair allocateGlobal(uint64_t size,
		const llvm::Value *allocSite)
	{ return allocate(size, false, true, allocSite); }


	std::vector<ObjectPair> allocateAlignedChopped(
		uint64_t size, unsigned pow2,
		const llvm::Value *allocSite);

	const ObjectState *allocateFixed(
		uint64_t address, uint64_t size,
		const llvm::Value *allocSite);

	const ObjectState *allocateAt(
		uint64_t address, uint64_t size,
		const llvm::Value *allocSite);

	virtual void bindObject(const MemoryObject *mo, ObjectState *os);
	virtual void unbindObject(const MemoryObject* mo);
	virtual void rebindObject(const MemoryObject* mo, ObjectState* os);


	const ObjectState* bindMemObj(
		const MemoryObject *mo, const Array *array = 0);
	ObjectState* bindMemObjWriteable(
		const MemoryObject *mo, const Array *array = 0);

	const ObjectState *bindStackMemObj(
		const MemoryObject *mo, const Array *array = 0);


	bool setupCallVarArgs(unsigned funcArgs, std::vector<ref<Expr> >& args);

	bool addConstraint(ref<Expr> constraint);
	bool merge(const ExecutionState &b);

  void copy(ObjectState* os, const ObjectState* reallocFrom, unsigned count);

  void commitIVC(const ref<ReadExpr>& re, const ref<ConstantExpr>& ce);

  ref<Expr>
  read(const ObjectState* obj, ref<Expr> offset, Expr::Width w) const
  { return obj->read(offset, w); }

  ref<Expr>
  read(const ObjectState* obj, unsigned offset, Expr::Width w) const
  { return obj->read(offset, w); }

  ref<Expr>
  readSymbolic(const ObjectState* obj, unsigned offset, Expr::Width w) const;

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

  void bindLocal(KInstruction *target, ref<Expr> value);
  ref<Expr> readLocal(KInstruction* target) const;
  bool hasLocal(KInstruction *target) const;
  void bindArgument(KFunction *kf, unsigned index, ref<Expr> value);

  void transferToBasicBlock(llvm::BasicBlock* dst, llvm::BasicBlock* src);
  void trackBranch(int condIndex, const KInstruction* ki);
  bool isReplayDone(void) const;
  bool pushHeapRef(HeapObject* heapObj)
  { return brChoiceSeq.push_heap_ref(heapObj); }

	void printFileLine(void);
	unsigned stepReplay(void);

	BranchTracker::iterator branchesBegin(void) const
	{ return brChoiceSeq.begin(); }

	BranchTracker::iterator branchesEnd(void) const
	{ return brChoiceSeq.end(); }

	BranchInfo branchLast(void) const;

	unsigned getPHISlot(void) const { return incomingBBIndex; }

	std::vector< SymbolicArray >::const_iterator symbolicsBegin(void) const
	{ return symbolics.begin(); }

	std::vector< SymbolicArray >::const_iterator symbolicsEnd(void) const
	{ return symbolics.end(); }

	stack_iter_ty stackBegin(void) { return stack.begin(); }
	stack_iter_ty stackEnd(void) { return stack.end(); }

	unsigned int getNumSymbolics(void) const { return symbolics.size(); }
	bool isConcrete(void) const;

	void assignSymbolics(const Assignment& a);

	void abortInstruction(void);

	/* translates state's registers into gdb format */
	/* for default llvm stuff, there's no register file, only
	 * stack frames with SSA variables */
	virtual void getGDBRegs(
		std::vector<uint8_t>& v,
		std::vector<bool>& is_conc) const
	{ v.clear(); is_conc.clear(); }

	virtual uint64_t getAddrPC(void) const { return (uint64_t)(&(*pc)); }
	std::string getArrName(const char* arrPrefix);
	void getConstraintLog(std::string& res) const;

	bool isShadowing(void) const { return is_shadowing; }
	void setShadow(uint64_t s) { is_shadowing = true; shadow_v = s; }
	void unsetShadow(void) { is_shadowing = false; }
private:
	static MemoryManager* mm;
	unsigned int num_allocs;

	// An ordered sequence of branches this state took thus far:
	BranchTracker brChoiceSeq;
	// used only if isCompactForm
	BranchTracker::iterator replayBrIter;

	unsigned incomingBBIndex;

	/// ordered list of symbolics: used to generate test cases.
	std::vector< SymbolicArray > symbolics;
	typedef std::map<const Array*, const MemoryObject*> arr2sym_map;
	arr2sym_map	arr2sym;

	uint64_t	prev_constraint_hash;

	// true iff this state is a mere placeholder for a real state
	bool	isCompactForm;
	bool	onFreshBranch;

	unsigned		canary;
	ConstraintManager	concrete_constraints;

	bool			is_shadowing;
	uint64_t		shadow_v;

};

}

#endif

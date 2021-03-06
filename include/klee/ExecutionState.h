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

#include "klee/Internal/ADT/ProtoPtr.h"
#include "klee/Constraints.h"
#include "klee/Expr.h"
#include "klee/util/ExprUtil.h"
#include "klee/Internal/Module/Cell.h"
#include "../../lib/Core/AddressSpace.h"
#include "../../lib/Core/StateTLB.h"
#include "../../lib/Core/BranchTracker.h"
#include "../../lib/Core/Terminator.h"
#include "klee/Internal/Module/KInstIterator.h"
#include "klee/Internal/ADT/TreeStream.h"
#include "../../lib/Core/Memory.h"
#include "../../lib/Core/CovSet.h"
#include "klee/CallStack.h"

#include <map>
#include <set>
#include <vector>

#include "klee/ExeStateBuilder.h"
#include "klee/StackFrame.h"

namespace klee
{
class Array;
class Assignment;
class Cell;
class KFunction;
class MemoryObject;
class PTreeNode;
struct InstructionInfo;
class MemoryManager;
class KInstruction;
class Terminator;

typedef uint64_t sid_t;

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
	: mo(in_mo), array(in_array), concretization(0), is_virt(false) {}

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

	void setVirtual(void) { is_virt = true; }
	bool isVirtual(void) const { return is_virt; }
private:
	ref<MemoryObject>	mo;
	ref<Array>		array;
	ref<ConcreteArray>	concretization;
	bool			is_virt;
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
	unsigned			depth;
	double				weight;
	StateTLB		stlb;	/* local tlb for when mmu calls to runtime */

	// pc - pointer to current instruction stream
	KInstIterator		pc, prevPC;
	CallStack		stack;
	ConstraintManager	constraints;
	mutable double		queryCost;
	AddressSpace		addressSpace;
	TreeOStream		symPathOS;
	uint64_t		lastNewInst;
	uint64_t		lastGlobalInstCount; // last stats::instructions
	uint64_t		totalInsts;
	unsigned		concretizeCount;
	ref<Expr>		prevForkCond;	// last condition to cause fork
	uint64_t		personalInsts;
	uint64_t		newInsts;

	// Number of malloc calls per callsite
	std::map<const llvm::Value*,unsigned> mallocIterations;

	// Ref counting for MemoryObject deallocation
	std::list<ref<MemoryObject> > memObjects;

	bool			coveredNew;
	bool			isReplay;	/* started in replay mode? */
	bool			isPartial;
	bool			isEnableMMU;

	bool forkDisabled;	/* Disables forking, set by user code. */

	std::map<const std::string*, std::set<unsigned> > coveredLines;
	std::shared_ptr<PTreeNode> ptreeNode;
	CovSet	covset;

	// for use with std::mem_fun[_ref] since they don't accept data members
	bool isCompact() const { return isCompactForm; }
	bool isNonCompact() const { return !isCompactForm; }

	unsigned int getNumAllocs(void) const { return num_allocs; }
	void setFreshBranch(void) { onFreshBranch = true; }
	void setOldBranch(void) { onFreshBranch = false; }
	bool isOnFreshBranch(void) const { return onFreshBranch; }
	void compact(void);
protected:
	ExecutionState();
	ExecutionState(KFunction *kf);
	// XXX total hack, just used to make a state so solver can
	// use on structure
	ExecutionState(const std::vector<ref<Expr> > &assumptions);
public:
	static void setMemoryManager(MemoryManager* in_mm) { mm = in_mm; }
	ExecutionState* copy(void) const;

	virtual ExecutionState* copy(const ExecutionState* es) const;

	virtual ~ExecutionState();

	static ExecutionState* createReplay(
		ExecutionState& initialState,
		const ReplayPath& replayPath);
	void joinReplay(const ReplayPath& replayPath);


	ExecutionState *branch(bool forReplay = false);
	ExecutionState *reconstitute(ExecutionState &initialStateCopy) const;

	KInstIterator getCaller(void) const;
	void dumpStack(std::ostream &os) const;

#define INVALID_STACK	(~((unsigned)0))
	virtual unsigned getStackDepth(void) const;
	void printConstraints(std::ostream& os) const;


	KFunction* getCurrentKFunc(void) const
	{
		if (stack.empty()) return NULL;
		return (stack.back()).kf;
	}

	void pushFrame(KInstIterator caller, KFunction *kf);
	void popFrame();
	void xferFrame(KFunction *kf);

	void addSymbolic(MemoryObject *mo, Array *array);

	const MemoryObject* findMemoryObject(const Array* a) const;

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

	bool setupCallVarArgs(
		unsigned funcArgs,
		const std::vector<ref<Expr>>& args);

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

  /* read the symbolic value of an object
   * (i.e., prior to any overwrites) */
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

  void bindLocal(KInstruction *target, ref<Expr> value);
  void bindArgument(KFunction *kf, unsigned index, ref<Expr> value);

  void transferToBasicBlock(const llvm::BasicBlock* dst, const llvm::BasicBlock* src);
  void trackBranch(int condIndex, const KInstruction* ki);
  bool isReplayDone(void) const;
  bool pushHeapRef(HeapObject* heapObj)
  { return brChoiceSeq.push_heap_ref(heapObj); }

	void printFileLine(void);
	unsigned stepReplay(void);
	unsigned peekReplay(void) const;
	unsigned getBrSeq(void) const;

	const BranchTracker& getBrTracker(void) const { return brChoiceSeq; }

	BranchTracker::iterator branchesBegin(void) const
	{ return brChoiceSeq.begin(); }

	BranchTracker::iterator branchesEnd(void) const
	{ return brChoiceSeq.end(); }

	BranchTracker::iterator branchesCur(void) const { return replayBrIter; }

	ReplayNode branchLast(void) const;

	unsigned getPHISlot(void) const { return incomingBBIndex; }

	typedef std::vector<SymbolicArray> syms_t;

	const syms_t& getSymbolics(void) const { return symbolics; }

	bool isConcrete(void) const;

	void assignSymbolics(const Assignment& a);
	void markSymbolicVirtual(const Array* a);

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

	virtual void inheritControl(ExecutionState& es);

	void printMinInstKFunc(std::ostream& os) const;
	unsigned getNumMinInstKFuncs(void) const { return min_kf_inst.size(); }

	/* number of nodes in replay head;
	 * returns 0 if not materialization of replay path */
	unsigned replayHeadLength(const ReplayPath& rp) const;

	sid_t getSID(void) const { return sid; }

	bool getOnFini(void) const { return term.get() != NULL; }
	Terminator* getFini(void) const { return term.get(); }
	void setFini(const Terminator& t) { term = ProtoPtr<Terminator>(t); }

	// Two states may execute differently, but have same memory contents
	// once finished. If the executions uses allocas differently, they'll
	// have different address spaces because of caching and fail to xchk.
	// So, clean up caching before xchk with this.
	void clearAllocaCache(void) {
		for (auto mo : alloca_victims) unbindObject(mo);
		alloca_victims.clear();
	}

private:
	void initFields(void);
	void updatePartSeed(Array* array);

	static MemoryManager	*mm;
	unsigned int		num_allocs;

	// An ordered sequence of branches this state took thus far:
	BranchTracker		brChoiceSeq;

	// used for isCompactForm and replay
	BranchTracker::iterator replayBrIter;

	unsigned incomingBBIndex;

	/// ordered list of symbolics: used to generate test cases.
	std::vector< SymbolicArray > symbolics;
	typedef std::map<const Array*, uint64_t> arr2addr_map;
	arr2addr_map	arr2addr;

	uint64_t	prev_constraint_hash;

	// true iff this state is a mere placeholder for a real state
	bool	isCompactForm;
	bool	onFreshBranch;

	unsigned		canary;

	bool			is_shadowing;
	uint64_t		shadow_v;

	typedef std::map<const KFunction*, uint64_t>	min_kf_inst_ty;
	min_kf_inst_ty		min_kf_inst;

	sid_t			sid;
	ProtoPtr<Terminator>	term;

	std::deque<const MemoryObject*>	alloca_victims;
};

typedef std::pair<ExecutionState*,ExecutionState*> StatePair;
typedef std::vector<ExecutionState*> StateVector;
}

#endif

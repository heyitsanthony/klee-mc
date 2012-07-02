//===-- Executor.h ----------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Class to perform actual execution, hides implementation details from external
// interpreter.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_EXECUTOR_H
#define KLEE_EXECUTOR_H

#include "klee/ExecutionState.h"
#include "klee/Interpreter.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/KInstruction.h"
#include "llvm/ADT/APFloat.h"
#include "SeedInfo.h"
#include "SeedCore.h"
#include <vector>
#include <string>
#include <map>
#include <set>
#include <list>

struct KTest;

namespace llvm {
  class BasicBlock;
  class BranchInst;
  class CallInst;
  class CallSite;
  class Constant;
  class ConstantExpr;
  class Function;
  class GlobalValue;
  class Instruction;
  class TargetData;
  class Twine;
  class Value;
  class VectorType;
}

namespace klee {
class Array;
class Cell;
class ConstantExpr;
class ExecutionState;
class ExeStateManager;
class ExternalDispatcher;
class Expr;
class Forks;
class Globals;
class InstructionInfoTable;
class KFunction;
class KInstIterator;
class MMU;
class MemoryManager;
class MemoryObject;
class ObjectState;
class PTree;
class Searcher;
class SpecialFunctionHandler;
struct StackFrame;
class StatsTracker;
class TimingSolver;
class TreeStreamWriter;
class BranchPredictor;
class WallTimer;

template<class T> class ref;

/// \todo Add a context object to keep track of data only live
/// during an instruction step. Should contain addedStates,
/// removedStates, and haltExecution, among others.

#define EXE_SWITCH_RLE_LIMIT	4

class Executor : public Interpreter
{
/* FIXME The executor shouldn't have friends. */
friend class ExeStateManager;
friend class BumpMergingSearcher;
friend class RandomPathSearcher;
friend class MergingSearcher;
friend class WeightedRandomSearcher;
friend class SpecialFunctionHandler;
friend class StatsTracker;

public:
	class Timer {
	public:
		Timer();
		virtual ~Timer();
		virtual void run() = 0;
	};

	typedef std::pair<ExecutionState*,ExecutionState*> StatePair;
	typedef std::vector<ExecutionState*> StateVector;
	typedef std::vector<
	std::pair<
		std::pair<const MemoryObject*, const ObjectState*>,
		ExecutionState*> > ExactResolutionList;

  /// Resolve a pointer to the memory objects it could point to the
  /// start of, forking execution when necessary and generating errors
  /// for pointers to invalid locations (either out of bounds or
  /// address inside the middle of objects).
  ///
  /// \param results[out] A list of ((MemoryObject,ObjectState),
  /// state) pairs for each object the given address can point to the
  /// beginning of.
  void resolveExact(ExecutionState &state,
                    ref<Expr> p,
                    ExactResolutionList &results,
                    const std::string &name);

  /// Return a unique constant value for the given expression in the
  /// given state, if it has one (i.e. it provably only has a single
  /// value). Otherwise return the original expression.
  ref<Expr> toUnique(const ExecutionState &state, ref<Expr> &e);

  // Fork current and return states in which condition holds / does
  // not hold, respectively. One of the states is necessarily the
  // current state, and one of the states may be null.
  StatePair fork(ExecutionState &current, ref<Expr> condition, bool isInternal);
  StateVector fork(
	ExecutionState &current,
	unsigned N, ref<Expr> conditions[], bool isInternal,
	bool isBranch = false);


  /// Get textual information regarding a memory address.
  std::string getAddressInfo(ExecutionState &state, ref<Expr> address) const;

  /// Return a constant value for the given expression, forcing it to
  /// be constant in the given state by adding a constraint if
  /// necessary. Note that this function breaks completeness and
  /// should generally be avoided.
  ///
  /// \param purpose An identify string to printed in case of concretization.
  ref<klee::ConstantExpr> toConstant(
  	ExecutionState &state, ref<Expr> e, const char *purpose,
	bool showLineInfo = true);

	/// Bind a constant value for e to the given target. NOTE: This
	/// function may fork state if the state has multiple seeds.
	virtual void executeGetValue(
		ExecutionState &state, ref<Expr> e, KInstruction *target);

	const KModule* getKModule(void) const { return kmodule; }
	KModule* getKModule(void) { return kmodule; }

	virtual void printStackTrace(
		ExecutionState& state, std::ostream& os) const;
	virtual std::string getPrettyName(llvm::Function* f) const;

	ExeStateSet::const_iterator beginStates(void) const;
	ExeStateSet::const_iterator endStates(void) const;

	virtual void stepStateInst(ExecutionState* &state);
	void notifyCurrent(ExecutionState *current);
	bool isHalted(void) const { return haltExecution; }

	MemoryManager	*memory;
private:
	class TimerInfo;

	static void deleteTimerInfo(TimerInfo*&);
	void handleMemoryUtilization(ExecutionState* &state);
	void handleMemoryPID(ExecutionState* &state);

protected:
	KModule		*kmodule;
	MMU		*mmu;
	Globals		*globals;
	BranchPredictor	*brPredict;

	struct XferStateIter
	{
		ref<Expr>	v;
		KInstruction	*ki;
		ExecutionState* free;
		llvm::Function*	f;
		StatePair 	res;
		unsigned	getval_c;
		unsigned	state_c;
		unsigned	badjmp_c;
	};

	virtual void xferIterInit(
		struct XferStateIter& iter,
		ExecutionState* state,
		KInstruction* ki);
	bool xferIterNext(struct XferStateIter& iter);

	virtual void executeInstruction(ExecutionState &state, KInstruction *ki);

	virtual void run(ExecutionState &initialState);
	virtual void runLoop();

	virtual void instRet(ExecutionState& state, KInstruction* ki);
	virtual void instAlloc(ExecutionState& state, KInstruction* ki);

	void retFromNested(ExecutionState& state, KInstruction* ki);

	/// bindInstructionConstants - Initialize any necessary per instruction
	/// constant values.
	void bindInstructionConstants(KInstruction *KI);

	virtual void printStateErrorMessage(
		ExecutionState& state,
		const std::string& message,
		std::ostream& os);

	// call error handler and terminate state, for execution errors
	// (things that should not be possible, like illegal instruction or
	// unlowered instrinsic, or are unsupported, like inline assembly)
	void terminateStateOnExecError(
		ExecutionState &state,
		const llvm::Twine &message,
		const llvm::Twine &info="")
	{ terminateStateOnError(state, message, "exec.err", info); }

  ref<ConstantExpr> getSmallSymAllocSize(ExecutionState &st, ref<Expr>& size);
  virtual void removePTreeState(
  	ExecutionState* es, ExecutionState** root_to_be_removed = 0);

  virtual ObjectState* makeSymbolic(
    ExecutionState& state,
    const MemoryObject* mo,
    ref<Expr> len,
    const char* arrPrefix = "arr");


  const Cell& eval(KInstruction *ki, unsigned idx, ExecutionState &st) const;

  virtual llvm::Function* getFuncByAddr(uint64_t addr) = 0;

  virtual void callExternalFunction(ExecutionState &state,
                            KInstruction *target,
                            llvm::Function *function,
                            std::vector< ref<Expr> > &arguments) = 0;

  virtual void executeCall(ExecutionState &state,
        KInstruction *ki,
        llvm::Function *f,
        std::vector< ref<Expr> > &arguments);

  virtual bool isInterestingTestCase(ExecutionState* st) const;

  InterpreterHandler	*interpreterHandler;
  llvm::TargetData	*target_data;
  llvm::Function	*dbgStopPointFn;
  StatsTracker		*statsTracker;
  PTree			*pathTree;
  TreeStreamWriter	*symPathWriter;
  TimingSolver		*solver;
  ExeStateManager	*stateManager;
  Forks			*forking;
  ExecutionState	*currentState;

private:
  std::vector<TimerInfo*>	timers;
  std::set<KFunction*>		bad_conc_kfuncs;

  /// When non-null the bindings that will be used for calls to
  /// klee_make_symbolic in order replay.
  const struct KTest *replayOut;

  /// When non-empty a list of lists of branch decisions to be used for replay.
  const std::list<ReplayPathType> *replayPaths;

  /// The index into the current \ref replayOut object.
  unsigned replayPosition;

  /// Disables forking, instead a random path is chosen. Enabled as
  /// needed to control memory usage. \see fork()
  bool atMemoryLimit;

  /// Disables forking, set by client. \see setInhibitForking()
  bool inhibitForking;

  /// Signals the executor to halt execution at the next instruction step.
  bool haltExecution;

  /// Forces only non-compact states to be chosen. Initially false,
  /// gets true when atMemoryLimit becomes true, and reset to false
  /// when memory has dropped below a certain threshold
  bool onlyNonCompact;

  ExecutionState* initialStateCopy;

  /// Whether implied-value concretization is enabled.
  /// Currently false, it is buggy (it needs to validate its writes).
  //  XXX I don't know what it means by validating writes. It
  //  looks good to me, so I enable by default --AJR
  bool ivcEnabled;

  /// Remembers the instruction count at the last memory limit operation.
  uint64_t lastMemoryLimitOperationInstructions;

  /// The maximum time to allow for a single stp query.
  double stpTimeout;

  bool isDebugIntrinsic(const llvm::Function *f);

  void instShuffleVector(ExecutionState& state, KInstruction* ki);
  void instExtractElement(ExecutionState& state, KInstruction* ki);
  void instInsertElement(ExecutionState& state, KInstruction *ki);
  void instBranch(ExecutionState& state, KInstruction* ki);
  void instBranchConditional(ExecutionState& state, KInstruction* ki);

  void markBranchVisited(
  	ExecutionState& state,
	KInstruction *ki,
	const StatePair& branches,
	const ref<Expr>& cond);
  void finalizeBranch(ExecutionState* st, llvm::BranchInst* bi, int branchIdx);

  void instCmp(ExecutionState& state, KInstruction* ki);
  ref<Expr> cmpScalar(
  	ExecutionState& state,
  	int pred, ref<Expr> left, ref<Expr> right, bool& ok);
  ref<Expr> cmpVector(
  	ExecutionState& state,
	int pred,
	llvm::VectorType* op_type,
	ref<Expr> left, ref<Expr> right,
	bool& ok);
  ref<Expr> sextVector(
	ExecutionState& state,
	ref<Expr> v,
	llvm::VectorType* srcTy,
	llvm::VectorType* dstTy);
  void instCall(ExecutionState& state, KInstruction* ki);
  void instGetElementPtr(ExecutionState& state, KInstruction *ki);

  void instSwitch(ExecutionState& state, KInstruction* ki);
  void forkSwitch(
  	ExecutionState&		state,
	llvm::BasicBlock	*parent_bb,
	const TargetTy&		defaultTarget,
	const TargetsTy&	targets);

  bool isFPPredicateMatched(
    llvm::APFloat::cmpResult CmpRes, llvm::CmpInst::Predicate pred);


  void printFileLine(ExecutionState &state, KInstruction *ki);

  void replayPathsIntoStates(ExecutionState& initialState);
  void killStates(ExecutionState* &state);

  void stepInstruction(ExecutionState &state);
  void removeRoot(ExecutionState* es);
  void replaceStateImmForked(ExecutionState* os, ExecutionState* ns);

  void executeCallNonDecl(
    ExecutionState &state,
    KInstruction *ki,
    llvm::Function *f,
    std::vector< ref<Expr> > &arguments);

  llvm::Function* executeBitCast(
	ExecutionState	&state,
	llvm::CallSite	&cs,
	llvm::ConstantExpr* ce,
	std::vector< ref<Expr> > &arguments);


  ObjectState* makeSymbolicReplay(
    ExecutionState& state, const MemoryObject* mo, ref<Expr> len);

  void doImpliedValueConcretization(ExecutionState &state,
                                    ref<Expr> e,
                                    ref<ConstantExpr> value);
  void commitIVC(
	ExecutionState	&state,
	const ref<ReadExpr>&	re,
	const ref<ConstantExpr>& ce);
  bool getSatAssignment(const ExecutionState& st, Assignment& a);


  void initTimers();
  void processTimers(ExecutionState *current, double maxInstTime);
  void processTimersDumpStates(void);

  void getSymbolicSolutionCex(const ExecutionState& state, ExecutionState& t);
public:
	Executor(InterpreterHandler *ie);
	virtual ~Executor();

	unsigned getNumStates(void) const;
	unsigned getNumFullStates(void) const;

	const InterpreterHandler& getHandler() { return *interpreterHandler; }
	TimingSolver* getSolver(void) { return solver; }
	virtual bool isStateSeeding(ExecutionState* s) const { return false; }

	/// Add the given (boolean) condition as a constraint on state. This
	/// function is a wrapper around the state's addConstraint function
	/// which also manages manages propogation of implied values,
	/// validity checks, and seed patching.
	virtual bool addConstraint(ExecutionState &state, ref<Expr> condition);
	void addConstrOrDie(ExecutionState &state, ref<Expr> condition);


	MemoryObject* findGlobalObject(const llvm::GlobalValue*) const;

	/* returns forked copy of symbolic state st; st is concretized */
	ExecutionState* concretizeState(ExecutionState& st);

	ObjectState* executeMakeSymbolic(
		ExecutionState &state,
		const MemoryObject *mo,
		const char* arrPrefix = "arr");

	ObjectState* executeMakeSymbolic(
		ExecutionState& state,
		const MemoryObject* mo,
		ref<Expr> len,
		const char* arrPrefix = "arr");

	// remove state from queue and delete
	virtual void terminateState(ExecutionState &state);
	// call exit handler and terminate state
	void terminateStateEarly(
		ExecutionState &state, const llvm::Twine &message);
	// call exit handler and terminate state
	void terminateStateOnExit(ExecutionState &state);
	// call error handler and terminate state
	void terminateStateOnError(
		ExecutionState &state,
		const llvm::Twine &message,
		const char *suffix,
		const llvm::Twine &longMessage="");

	// XXX should just be moved out to utility module
	ref<klee::ConstantExpr> evalConstant(llvm::Constant *c)
	{ return evalConstant(kmodule, globals, c); }

	static ref<klee::ConstantExpr> evalConstant(
		const KModule* km, const Globals* gm, llvm::Constant *c);


	/// Allocate and bind a new object in a particular state. NOTE: This
	/// function may fork.
	///
	/// \param isLocal Flag to indicate if the object should be
	/// automatically deallocated on function return (this also makes it
	/// illegal to free directly).
	///
	/// \param target Value at which to bind the base address of the new
	/// object.
	///
	/// \param reallocFrom If non-zero and the allocation succeeds,
	/// initialize the new object from the given one and unbind it when
	/// done (realloc semantics). The initialized bytes will be the
	/// minimum of the size of the old and new objects, with remaining
	/// bytes initialized as specified by zeroMemory.
	void executeAlloc(
		ExecutionState &state,
		ref<Expr> size,
		bool isLocal,
		KInstruction *target,
		bool zeroMemory,
		ObjectPair reallocFrom);

	void executeAlloc(
		ExecutionState &state,
		ref<Expr> size,
		bool isLocal,
		KInstruction *target,
		bool zeroMemory=false)
	{ executeAlloc(
		state, size, isLocal, target, zeroMemory, ObjectPair(0,0)); }

	void executeAllocSymbolic(
		ExecutionState &state,
		ref<Expr> size,
		bool isLocal,
		KInstruction *target,
		bool zeroMemory,
		ObjectPair reallocFrom);

	void executeAllocSymbolic(
		ExecutionState &state,
		ref<Expr> size,
		bool isLocal,
		KInstruction *target,
		bool zeroMemory)
	{ executeAllocSymbolic(
		state, size, isLocal, target, zeroMemory, ObjectPair(0,0)); }

	void executeAllocConst(
		ExecutionState &state,
		uint64_t sz,
		bool isLocal,
		KInstruction *target,
		bool zeroMemory,
		ObjectPair reallocFrom);

	void executeAllocConst(
		ExecutionState &state,
		uint64_t size,
		bool isLocal,
		KInstruction *target,
		bool zeroMemory)
	{ executeAllocConst(
		state, size, isLocal, target, zeroMemory, ObjectPair(0,0)); }


	/// Free the given address with checking for errors. If target is
	/// given it will be bound to 0 in the resulting states (this is a
	/// convenience for realloc). Note that this function can cause the
	/// state to fork and that \ref state cannot be safely accessed
	/// afterwards.
	void executeFree(
		ExecutionState &state,
		ref<Expr> address,
		KInstruction *target = 0);

	ref<klee::ConstantExpr> evalConstantExpr(llvm::ConstantExpr *ce)
	{ return evalConstantExpr(kmodule, globals, ce); }

	static ref<klee::ConstantExpr> evalConstantExpr(
		const KModule* km,
		const Globals* gm,
		llvm::ConstantExpr *ce);

	virtual void setSymbolicPathWriter(TreeStreamWriter *tsw)
	{ symPathWriter = tsw; }
	TreeStreamWriter* getSymbolicPathWriter(void) { return symPathWriter; }

	virtual void setReplayOut(const struct KTest *out)
	{
		assert(!replayPaths && "cannot replay both buffer and path");
		replayOut = out;
		replayPosition = 0;
	}

	virtual void setReplayPaths(const std::list<ReplayPathType>* paths)
	{
		assert(!replayOut && "cannot replay both buffer and path");
		replayPaths = paths;
	}

	bool isReplayOut(void) const { return (replayOut != NULL); }
	bool isReplayPaths(void) const { return (replayPaths != NULL); }

	/*** Runtime options ***/

	virtual void setHaltExecution(bool v) { haltExecution = v; }
	virtual void setInhibitForking(bool v) { inhibitForking = v; }
	bool getInhibitForking(void) const { return inhibitForking; }

	/*** State accessor methods ***/

	virtual unsigned getSymbolicPathStreamID(const ExecutionState &state);

	virtual bool getSymbolicSolution(
		const ExecutionState &state,
		std::vector<
			std::pair<std::string,
			std::vector<unsigned char> > > &res);

	virtual void getCoveredLines(
		const ExecutionState &state,
		std::map<const std::string*, std::set<unsigned> > &res)
	{ res = state.coveredLines; }

	StatsTracker* getStatsTracker(void) const { return statsTracker; }

	void yield(ExecutionState& state);

	InterpreterHandler *getInterpreterHandler(void) const
	{ return interpreterHandler; }

	const struct KTest *getReplayOut(void) const { return replayOut; }
	bool isAtMemoryLimit(void) const { return atMemoryLimit; }
	PTree* getPTree(void) { return pathTree; }
	ExeStateManager* getStateManager(void) { return stateManager; }
	const Forks* getForking(void) const { return forking; }

	ExecutionState* getCurrentState(void) const { return currentState; }
	bool hasState(const ExecutionState* es) const;

 	void addTimer(Timer *timer, double rate);
	const Globals* getGlobals(void) const { return globals; }
	StatsTracker* getStatsTracker(void) { return statsTracker; }

	/* XXX XXX XXX get rid of me!! XXX XXX */
	SeedMapType	dummySeedMap;
	virtual SeedMapType& getSeedMap(void) { return dummySeedMap; }
};

}
// End klee namespace

#endif

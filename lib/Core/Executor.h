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
#include "llvm/Support/CallSite.h"
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
  class Constant;
  class ConstantExpr;
  class Function;
  class GlobalValue;
  class GlobalVariable;
  class Instruction;
  class TargetData;
  class Twine;
  class Value;
}

namespace klee {  
  class Array;
  class Cell;
  class EquivalentStateEliminator;
  class ExecutionState;
  class ExeStateManager;
  class ExternalDispatcher;
  class Expr;
  class InstructionInfoTable;
  class KFunction;
  class KInstruction;
  class KInstIterator;
  class MemoryManager;
  class MemoryObject;
  class ObjectState;
  class PTree;
  class Searcher;
  class SeedInfo;
  class SpecialFunctionHandler;
  class StackFrame;
  class StatsTracker;
  class TimingSolver;
  class TreeStreamWriter;
  template<class T> class ref;

  /// \todo Add a context object to keep track of data only live
  /// during an instruction step. Should contain addedStates,
  /// removedStates, and haltExecution, among others.

class Executor : public Interpreter {
  /* FIXME The executor shouldn't have friends. */
  friend class ExeStateManager;
  friend class BumpMergingSearcher;
  friend class MergingSearcher;
  friend class RandomPathSearcher;
  friend class OwningSearcher;
  friend class WeightedRandomSearcher;
  friend class SpecialFunctionHandler;
  friend class StatsTracker;

public:
  class Timer {
  public:
    Timer();
    virtual ~Timer();

    /// The event callback.
    virtual void run() = 0;
  };

  typedef std::pair<ExecutionState*,ExecutionState*> StatePair;
  typedef std::vector<ExecutionState*> StateVector;

  const KModule* getKModule(void) const { return kmodule; }


private:
  class TimerInfo;
  static void deleteTimerInfo(TimerInfo*&);
  void runLoop(void);
  void handleMemoryUtilization(ExecutionState* &state);

protected:
  KModule *kmodule;

  void initializeGlobalObject(
    ExecutionState &state,
    ObjectState *os, 
    llvm::Constant *c,
    unsigned offset);

  void initializeGlobals(ExecutionState &state);

  virtual void executeInstruction(ExecutionState &state, KInstruction *ki);

  virtual void run(ExecutionState &initialState);
  virtual void instRet(ExecutionState& state, KInstruction* ki);
  void instRetFromNested(ExecutionState& state, KInstruction* ki);

  /// bindInstructionConstants - Initialize any necessary per instruction
  /// constant values.
  void bindInstructionConstants(KInstruction *KI);
  void bindArgument(KFunction *kf, 
                    unsigned index,
                    ExecutionState &state,
                    ref<Expr> value);
  ObjectState *bindObjectInState(ExecutionState &state, const MemoryObject *mo,
                                 bool isLocal, const Array *array = 0);
  void bindLocal(KInstruction *target, 
                 ExecutionState &state, 
                 ref<Expr> value);

  StatePair fork(ExecutionState &current, ref<Expr> condition, bool isInternal);

  // remove state from queue and delete
  void terminateState(ExecutionState &state);
  // call exit handler and terminate state
  void terminateStateEarly(ExecutionState &state, const llvm::Twine &message);
  // call exit handler and terminate state
  void terminateStateOnExit(ExecutionState &state);
  // call error handler and terminate state
  void terminateStateOnError(ExecutionState &state, 
                             const llvm::Twine &message,
                             const char *suffix,
                             const llvm::Twine &longMessage="");

  // call error handler and terminate state, for execution errors
  // (things that should not be possible, like illegal instruction or
  // unlowered instrinsic, or are unsupported, like inline assembly)
  void terminateStateOnExecError(ExecutionState &state, 
                                 const llvm::Twine &message,
                                 const llvm::Twine &info="") {
    terminateStateOnError(state, message, "exec.err", info);
  }

  /// Return a unique constant value for the given expression in the
  /// given state, if it has one (i.e. it provably only has a single
  /// value). Otherwise return the original expression.
  ref<Expr> toUnique(const ExecutionState &state, ref<Expr> &e);

  /// Resolve a pointer to the memory objects it could point to the
  /// start of, forking execution when necessary and generating errors
  /// for pointers to invalid locations (either out of bounds or
  /// address inside the middle of objects).
  ///
  /// \param results[out] A list of ((MemoryObject,ObjectState),
  /// state) pairs for each object the given address can point to the
  /// beginning of.
  typedef std::vector< std::pair<std::pair<const MemoryObject*, const ObjectState*>, 
                                 ExecutionState*> > ExactResolutionList;
  void resolveExact(ExecutionState &state,
                    ref<Expr> p,
                    ExactResolutionList &results,
                    const std::string &name);

  /// Get textual information regarding a memory address.
  std::string getAddressInfo(ExecutionState &state, ref<Expr> address) const;


  virtual const Cell& eval(
    KInstruction *ki,
    unsigned index,
    ExecutionState &state) const = 0;

  Function* getCalledFunction(CallSite &cs, ExecutionState &state);

  virtual void callExternalFunction(ExecutionState &state,
                            KInstruction *target,
                            llvm::Function *function,
                            std::vector< ref<Expr> > &arguments) = 0;

  void transferToBasicBlock(
    llvm::BasicBlock *dst, 
    llvm::BasicBlock *src,
    ExecutionState &state);

  void executeSymbolicFuncPtr(
	ExecutionState &state, 
	KInstruction *ki,
	std::vector< ref<Expr> > &arguments);

  void executeCall(ExecutionState &state, 
        KInstruction *ki,
        llvm::Function *f,
        std::vector< ref<Expr> > &arguments);



  InterpreterHandler *interpreterHandler;
  TargetData* target_data;
  llvm::Function* dbgStopPointFn; 
  StatsTracker *statsTracker;
  PTree *processTree;
  TreeStreamWriter *symPathWriter;
  TimingSolver *solver;


  MemoryManager *memory;
  ExeStateManager* stateManager;

  typedef std::map<const llvm::GlobalValue*, ref<ConstantExpr> > globaladdr_map;
  typedef std::map<const llvm::GlobalValue*, MemoryObject*> globalobj_map;
  /// Map of globals to their representative memory object.
  globalobj_map globalObjects;
  /// Map of globals to their bound address. This also includes
  /// globals that have no representative object (i.e. functions).
  globaladdr_map globalAddresses;

  /// The set of legal function addresses, used to validate function
  /// pointers. We use the actual Function* address as the function address.
  std::set<uint64_t> legalFunctions;

private:
  std::vector<TimerInfo*> timers;

  /// When non-empty the Executor is running in "seed" mode. The
  /// states in this map will be executed in an arbitrary order
  /// (outside the normal search interface) until they terminate. When
  /// the states reach a symbolic branch then either direction that
  /// satisfies one or more seeds will be added to this map. What
  /// happens with other states (that don't satisfy the seeds) depends
  /// on as-yet-to-be-determined flags.
  typedef  std::map<ExecutionState*, std::vector<SeedInfo> > SeedMapType;
  SeedMapType seedMap;
  
  /// When non-null the bindings that will be used for calls to
  /// klee_make_symbolic in order replay.
  const struct KTest *replayOut;

  /// When non-empty a list of lists of branch decisions to be used for replay.
  const std::list<ReplayPathType> *replayPaths;

  /// The index into the current \ref replayOut object.
  unsigned replayPosition;

  /// When non-null a list of "seed" inputs which will be used to
  /// drive execution.
  const std::vector<struct KTest *> *usingSeeds;  

  /// Disables forking, instead a random path is chosen. Enabled as
  /// needed to control memory usage. \see fork()
  bool atMemoryLimit;

  /// Disables forking, set by client. \see setInhibitForking()
  bool inhibitForking;

  /// Signals the executor to halt execution at the next instruction
  /// step.
  bool haltExecution;  
 
  /// Forces only non-compact states to be chosen. Initially false,
  /// gets true when atMemoryLimit becomes true, and reset to false
  /// when memory has dropped below a certain threshold
  bool onlyNonCompact;

  ExecutionState* initialStateCopy;

  /// Whether implied-value concretization is enabled. Currently
  /// false, it is buggy (it needs to validate its writes).
  bool ivcEnabled;

  /// Remembers the instruction count at the last memory limit operation.
  uint64_t lastMemoryLimitOperationInstructions;

  /// The maximum time to allow for a single stp query.
  double stpTimeout;  

  bool isDebugIntrinsic(const Function *f);

  void instBranch(ExecutionState& state, KInstruction* ki);
  void instCmp(ExecutionState& state, KInstruction* ki);
  void instCall(ExecutionState& state, KInstruction* ki);
  void instSwitch(ExecutionState& state, KInstruction* ki);
  void instUnwind(ExecutionState& state);

  bool isFPPredicateMatched(
    APFloat::cmpResult CmpRes, CmpInst::Predicate pred);


  void printFileLine(ExecutionState &state, KInstruction *ki);

  void replayPathsIntoStates(ExecutionState& initialState);
  void runState(ExecutionState* &state);
  void killStates(ExecutionState* &state);
  void compactStates(ExecutionState* &state);
  bool seedRun(ExecutionState& initialState);
  void seedRunOne(ExecutionState* &lastState);
  void updateStates(ExecutionState *current);

  typedef std::vector<SeedInfo>::iterator SeedInfoIterator;
  bool getSeedInfoIterRange(
    ExecutionState* s, SeedInfoIterator &b, SeedInfoIterator& e);

  // Given a concrete object in our [klee's] address space, add it to 
  // objects checked code can reference.
  MemoryObject *addExternalObject(ExecutionState &state, void *addr, 
                                  unsigned size, bool isReadOnly);
  inline void splitProcessTree(PTreeNode* n, ExecutionState* a,
                               ExecutionState* b);

  void stepInstruction(ExecutionState &state);
  void removePTreeState(
  	ExecutionState* es, ExecutionState** root_to_be_removed = 0);
  void removeRoot(ExecutionState* es);
  void replaceStateImmForked(ExecutionState* os, ExecutionState* ns);

  bool setupCallVarArgs(
    ExecutionState& state,
    unsigned funcArgs,
    std::vector<ref<Expr> >& arguments);
  void executeCallNonDecl(
    ExecutionState &state, 
    KInstruction *ki,
    Function *f,
    std::vector< ref<Expr> > &arguments);
  void executeBitCast(
	ExecutionState	&state, 
	CallSite	&cs,
	llvm::ConstantExpr* ce,
	std::vector< ref<Expr> > &arguments);


  // do address resolution / object binding / out of bounds checking
  // and perform the operation
  void executeMemoryOperation(ExecutionState &state,
                              bool isWrite,
                              ref<Expr> address,
                              ref<Expr> value /* undef if read */,
                              KInstruction *target /* undef if write */);
  bool memOpFast(
    ExecutionState& state,
    bool isWrite,
    ref<Expr> address,
    ref<Expr> value,
    KInstruction* target);

  ExecutionState* getUnboundState(
    ExecutionState* unbound,
    ObjectPair& resolution,
    bool isWrite,
    ref<Expr> address,
    unsigned bytes,
    Expr::Width& type,
    ref<Expr> value,
    KInstruction* target);

  void memOpError(
    ExecutionState& state,
    bool isWrite,
    ref<Expr> address,
    ref<Expr> value,
    KInstruction* target);


  void mergeStringStates(ref<Expr>& readExpr);
  bool isStrcmpMatch(
    const Expr  *expr,
    unsigned int idx, const std::string& arrName,
    unsigned int& re_idx, unsigned int& cmp_val);

  void executeMakeSymbolic(ExecutionState &state, const MemoryObject *mo);
  void executeMakeSymbolic(
    ExecutionState& state, const MemoryObject* mo, ref<Expr> len);
  void makeSymbolicReplay(
    ExecutionState& state, const MemoryObject* mo, ref<Expr> len);
  void makeSymbolic(
    ExecutionState& state, const MemoryObject* mo, ref<Expr> len);

  bool isForkingCondition(ExecutionState& current, ref<Expr> condition);
  bool isForkingCallPath(CallPathNode* cpn);
  StateVector fork(ExecutionState &current,
                   unsigned N, ref<Expr> conditions[], bool isInternal,
                   bool isBranch = false);
  bool forkSetupNoSeeding(
    ExecutionState& current,
    unsigned N, std::vector<bool>& res,
    bool isInternal,
    unsigned& validTargets, bool& forkCompact, bool& wasReplayed);
  void forkSetupSeeding( 
    ExecutionState& current,
    unsigned N, ref<Expr> conditions[],
    std::vector<bool>& res,
    std::vector<std::list<SeedInfo> >& resSeeds,
    unsigned& validTargets);


#if 0
  /// Create a new state where each input condition has been added as
  /// a constraint and return the results. The input state is included
  /// as one of the results. Note that the output vector may included
  /// NULL pointers for states which were unable to be created.
  void branch(ExecutionState &state, 
              const std::vector< ref<Expr> > &conditions,
              std::vector<ExecutionState*> &result);

  // Fork current and return states in which condition holds / does
  // not hold, respectively. One of the states is necessarily the
  // current state, and one of the states may be null.
  StatePair fork(ExecutionState &current, ref<Expr> condition, bool isInternal);
#endif

  /// Add the given (boolean) condition as a constraint on state. This
  /// function is a wrapper around the state's addConstraint function
  /// which also manages manages propogation of implied values,
  /// validity checks, and seed patching.
  void addConstraint(ExecutionState &state, ref<Expr> condition);

  // Called on [for now] concrete reads, replaces constant with a symbolic
  // Used for testing.
  ref<Expr> replaceReadWithSymbolic(ExecutionState &state, ref<Expr> e);

  ref<klee::ConstantExpr> evalConstantExpr(llvm::ConstantExpr *ce);

  /// Return a constant value for the given expression, forcing it to
  /// be constant in the given state by adding a constraint if
  /// necessary. Note that this function breaks completeness and
  /// should generally be avoided.
  ///
  /// \param purpose An identify string to printed in case of concretization.
  ref<klee::ConstantExpr> toConstant(ExecutionState &state, ref<Expr> e, 
                                     const char *purpose);

  /// Bind a constant value for e to the given target. NOTE: This
  /// function may fork state if the state has multiple seeds.
  void executeGetValue(ExecutionState &state, ref<Expr> e, KInstruction *target);

  void handlePointsToObj(ExecutionState &state, 
                         KInstruction *target, 
                         const std::vector<ref<Expr> > &arguments);

  void doImpliedValueConcretization(ExecutionState &state,
                                    ref<Expr> e,
                                    ref<ConstantExpr> value);

  /// Add a timer to be executed periodically.
  ///
  /// \param timer The timer object to run on firings.
  /// \param rate The approximate delay (in seconds) between firings.
  void addTimer(Timer *timer, double rate);

  void initTimers();
  void processTimers(ExecutionState *current,
                     double maxInstTime);
  void processTimersDumpStates(void);
  
  void getSymbolicSolutionCex(const ExecutionState& state, ExecutionState& t);

  bool seedObject(
    ExecutionState& state, SeedInfo& si,
    const MemoryObject* mo, const Array* array);

public:
  Executor(const InterpreterOptions &opts, InterpreterHandler *ie);
  virtual ~Executor();

  const InterpreterHandler& getHandler() { return *interpreterHandler; }

  // XXX should just be moved out to utility module
  ref<klee::ConstantExpr> evalConstant(llvm::Constant *c);

  virtual void setSymbolicPathWriter(TreeStreamWriter *tsw) {
    symPathWriter = tsw;
  }      

  virtual void setReplayOut(const struct KTest *out) {
    assert(!replayPaths && "cannot replay both buffer and path");
    replayOut = out;
    replayPosition = 0;
  }

  virtual void setReplayPaths(const std::list<ReplayPathType>* paths) {
    assert(!replayOut && "cannot replay both buffer and path");
    replayPaths = paths;
  }

  virtual void useSeeds(const std::vector<struct KTest *> *seeds) { 
    usingSeeds = seeds;
  }

  /*** Runtime options ***/
  
  virtual void setHaltExecution(bool value) {
    haltExecution = value;
  }

  virtual void setInhibitForking(bool value) {
    inhibitForking = value;
  }

  /*** State accessor methods ***/

  virtual unsigned getSymbolicPathStreamID(const ExecutionState &state);

  virtual void getConstraintLog(const ExecutionState &state,
                                std::string &res,
                                bool asCVC = false);

  virtual bool getSymbolicSolution(const ExecutionState &state, 
                                   std::vector< 
                                   std::pair<std::string,
                                   std::vector<unsigned char> > >
                                   &res);

  virtual void getCoveredLines(const ExecutionState &state,
                               std::map<const std::string*, std::set<unsigned> > &res);
};

} // End klee namespace

#endif

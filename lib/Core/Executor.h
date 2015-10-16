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
#include "klee/Internal/ADT/LimitedStream.h"
#include <llvm/ADT/APFloat.h>
#include "Terminator.h"
#include "SeedInfo.h"
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
  class DataLayout;
  class Twine;
  class Value;
  class VectorType;
}

extern uint32_t DebugPrintInstructions;

namespace klee {
class Array;
class Cell;
class Replay;
class Terminator;
class ConstantExpr;
class ExecutionState;
class ExeStateManager;
class ExternalDispatcher;
class Expr;
class Forks;
class Globals;
class InstructionInfoTable;
class KFunction;
class MMU;
class MemoryManager;
class MemoryObject;
class ObjectState;
class SpecialFunctionHandler;
struct StackFrame;
class StatsTracker;
class StateSolver;
class TreeStreamWriter;
class BranchPredictor;
class WallTimer;

/// \todo Add a context object to keep track of data only live
/// during an instruction step. Should contain addedStates,
/// removedStates, and haltExecution, among others.
class Executor : public Interpreter
{
/* FIXME The executor shouldn't have friends. */
friend class ExeStateManager;
friend class SpecialFunctionHandler;
friend class StatsTracker;

public:
	class Timer {
	public:
		Timer() {}
		virtual ~Timer() {}
		virtual void run() = 0;
	};

	class TimerInfo {
	public:
		TimerInfo(std::unique_ptr<Timer> _timer, double _rate);
		bool mayFire(double now) const { return now > nextFireTime; }
		bool fire(double now) {
			if (mayFire(now)) {
				forceFire(now);
				return true;
			}
			return false;
		}

		void forceFire(double now) {
			timer->run();
			nextFireTime = rate + now;
		}
	private:
		std::unique_ptr<Timer> timer;
		const double rate;  /// Approximate delay per timer firing.
		double nextFireTime; /// Wall time for next firing.
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
  ref<Expr> toUnique(const ExecutionState &state, const ref<Expr> &e);

  // Fork current and return states in which condition holds / does
  // not hold, respectively. One of the states is necessarily the
  // current state, and one of the states may be null.
  StatePair fork(ExecutionState &current, ref<Expr> condition, bool isInternal);
  ExecutionState* pureFork(ExecutionState& es, bool compact = false);
  StateVector fork(
	ExecutionState &current,
	std::vector<ref<Expr>> conditions,
	bool isInternal,
	bool isBranch = false);

	/* kf = NULL => exhaust state, return whether got function */
	bool runToFunction(ExecutionState* es, const KFunction *kf);
	void exhaustState(ExecutionState* es) { runToFunction(es, NULL); }


	/// Get textual info regarding a memory address.
	std::string getAddressInfo(ExecutionState &st, ref<Expr> addr) const;

	/// Return a constant value for the given expression, forcing it to
	/// be constant in the given state by adding a constraint if
	/// necessary. Note that this function breaks completeness and
	/// should generally be avoided.
	///
	/// \param purpose string printed in case of concretization.
	ref<klee::ConstantExpr> toConstant(
		ExecutionState &state, ref<Expr> e, const char *purpose,
		bool showLineInfo = true);

	/// Bind a constant value for e to the given target. NOTE: This
	/// function may fork state if the state has multiple seeds.
	virtual void executeGetValue(
		ExecutionState &state,
		ref<Expr> e,
		KInstruction *target,
		ref<Expr> pred = 0);

	const KModule* getKModule(void) const { return kmodule; }
	KModule* getKModule(void) { return kmodule; }
	SpecialFunctionHandler* getSFH(void) { return sfh; }
	void addModule(llvm::Module* m);

	virtual void printStackTrace(
		const ExecutionState& state, std::ostream& os) const;

	ExeStateSet::const_iterator beginStates(void) const;
	ExeStateSet::const_iterator endStates(void) const;

	virtual void stepStateInst(ExecutionState* &state);
	void commitQueue(ExecutionState *current = nullptr);
	bool isHalted(void) const { return haltExecution; }

	MMU* getMMU(void) const { return mmu; }
	void retFromNested(ExecutionState& state, KInstruction* ki);
	ExecutionState* getInitialState(void) { return initialStateCopy; }

	void addFiniFunction(llvm::Function* f);
	void addInitFunction(llvm::Function* f);

	virtual llvm::Function* getFuncByAddr(uint64_t addr) = 0;

	std::unique_ptr<MemoryManager>	memory;
private:
	void handleMemoryUtilization(ExecutionState* &state);
	void handleMemoryPID(ExecutionState* &state);
	void setupInitFuncs(ExecutionState& initState);

protected:
	KModule				*kmodule;
	MMU				*mmu;
	std::unique_ptr<Globals>	globals;
	std::unique_ptr<BranchPredictor> brPredict;

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
	virtual void debugPrintInst(ExecutionState& st);

	virtual void run(ExecutionState &initialState);
	virtual void runLoop();
	void step(void);

	/* returns false if unable to start finalization */
	virtual bool startFini(ExecutionState& state);

	virtual void instCall(ExecutionState& state, KInstruction* ki);
	virtual void instRet(ExecutionState& state, KInstruction* ki);
	virtual void instAlloc(ExecutionState& state, KInstruction* ki);
	virtual void instBranchConditional(
		ExecutionState& state, KInstruction* ki);

	virtual void replaceStateImmForked(ExecutionState* os, ExecutionState* ns);

	virtual void callExternalFunction(
		ExecutionState &state,
		KInstruction *target,
		llvm::Function *function,
		std::vector< ref<Expr> > &arguments) = 0;

	void executeCall(
		ExecutionState &state,
		KInstruction *ki,
		llvm::Function *f,
		std::vector< ref<Expr> > &arguments);

	virtual const ref<Expr> eval(
		KInstruction *ki,
		unsigned idx,
		ExecutionState &st) const;

	virtual StateSolver* createSolverChain(
		double timeout,
		const std::string& qPath,
		const std::string& logPath);


	InterpreterHandler	*interpreterHandler;
	llvm::DataLayout	*data_layout;
	std::unique_ptr<StatsTracker> statsTracker;
	TreeStreamWriter	*symPathWriter;
	StateSolver		*solver, *fastSolver;
	ExeStateManager		*stateManager;
	Forks			*forking;
	ExecutionState		*currentState;
	SpecialFunctionHandler	*sfh;

	/// Signals the executor to halt execution at the next instruction step.
	bool haltExecution;
private:
	std::vector<std::unique_ptr<TimerInfo>>	timers;

	/* functions which caused bad concretizations */
	std::set<KFunction*>		bad_conc_kfuncs;

	Replay	*replay;

	/// Disables forking, instead a random path is chosen. Enabled as
	/// needed to control memory usage. \see fork()
	bool atMemoryLimit;

	ExecutionState* initialStateCopy;

	/// Whether implied-value concretization is enabled.
	bool ivcEnabled;

	/// Remembers the instruction count at the last memory limit operation.
	uint64_t lastMemoryLimitOperationInstructions;

	void instInsertValue(ExecutionState& state, KInstruction* ki);
	void instShuffleVector(ExecutionState& state, KInstruction* ki);
	static ref<Expr> instShuffleVectorEvaled(
		llvm::VectorType	*vt,
		const ref<Expr>		&in_v_lo,
		const ref<Expr>		&in_v_hi,
		const ref<Expr>		&in_v_perm);

	void instExtractElement(ExecutionState& state, KInstruction* ki);
	void instInsertElement(ExecutionState& state, KInstruction *ki);
	void instBranch(ExecutionState& state, KInstruction* ki);

  void markBranchVisited(
  	ExecutionState& state,
	KInstruction *ki,
	const StatePair& branches,
	const ref<Expr>& cond);
	void finalizeBranch(ExecutionState* st, llvm::BranchInst* bi, int branchIdx);

	void instCmp(ExecutionState& state, KInstruction* ki);
	ref<Expr> cmpScalar(
		ExecutionState& state, int pred,
		ref<Expr>& left, ref<Expr>& right);
	ref<Expr> cmpVector(
		ExecutionState& state,
		int pred,
		llvm::VectorType* op_type,
		ref<Expr> left, ref<Expr> right);
	ref<Expr> extVector(
		ExecutionState& state,
		ref<Expr> v,
		llvm::VectorType* srcTy,
		llvm::VectorType* dstTy,
		bool is_zext);
	void instGetElementPtr(ExecutionState& state, KInstruction *ki);

	void instSwitch(ExecutionState& state, KInstruction* ki);
	void forkSwitch(
		ExecutionState&		state,
		KInstruction		*ki,
		const TargetTy&		defaultTarget,
		const TargetsTy&	targets);

	bool isFPPredicateMatched(
		llvm::APFloat::cmpResult CmpRes, llvm::CmpInst::Predicate pred);

	void printFileLine(ExecutionState &state, KInstruction *ki);

	void killStates(ExecutionState* &state);

	void stepInstruction(ExecutionState &state);
	void removeRoot(ExecutionState* es);

	llvm::Function* executeBitCast(
		ExecutionState	&state,
		llvm::CallSite	&cs,
		llvm::ConstantExpr* ce,
		std::vector< ref<Expr> > &arguments);

	bool getSatAssignment(const ExecutionState& st, Assignment& a);

	void initTimers();
	void processTimers(ExecutionState *current, double maxInstTime);
	void processTimersDumpStates(void);
	void flushTimers(void);

	void getSymbolicSolutionCex(
		const ExecutionState& state, ExecutionState& t);

public:
	Executor(InterpreterHandler *ie);
	virtual ~Executor();

	unsigned getNumStates(void) const;
	unsigned getNumFullStates(void) const;

	const InterpreterHandler& getHandler() { return *interpreterHandler; }
	StateSolver* getSolver(void) { return solver; }
	void setSolver(StateSolver* s) { solver = s; }
	virtual bool isStateSeeding(ExecutionState* s) const { return false; }

	void executeCallNonDecl(
		ExecutionState &state,
		llvm::Function *f,
		std::vector< ref<Expr> > &arguments);

	void doImpliedValueConcretization(
		ExecutionState &state, ref<Expr> e, ref<ConstantExpr> v);

	/// Add the given (boolean) condition as a constraint on state. This
	/// function is a wrapper around the state's addConstraint function
	/// which also manages manages propogation of implied values,
	/// validity checks, and seed patching.
	virtual bool addConstraint(ExecutionState &state, ref<Expr> condition);
	void addConstrOrDie(ExecutionState &state, ref<Expr> condition);


	/* returns forked copy of symbolic state st; st is concretized */
	ExecutionState* concretizeState(
		ExecutionState& st,
		ref<Expr> bad_expr = NULL);

	virtual ObjectState* makeSymbolic(
		ExecutionState &state,
		const MemoryObject *mo,
		const char* arrPrefix = "arr");

	// remove state from queue and delete
	virtual void terminate(ExecutionState &state);

	// Terminate with a termination info packet. Necessary
	// for producing test cases with information.
	virtual void terminateWith(Terminator& term, ExecutionState& state);

// exe, state, message
#define TERMINATE_EARLY(_x,y,m)	\
	do { TermEarly t(_x,m); (_x)->terminateWith(t, y); } while (0)
#define TERMINATE_EXIT(_x,y) do { TermExit t(_x); (_x)->terminateWith(t, y); } while (0)

// exe, state, message, suffix, longmsg="", always emit=false
#define TERMINATE_ERROR_LONG(_x,s,m,suf,lm,em)	\
	do { TermError t(_x,s,m,suf,lm,em); (_x)->terminateWith(t, s); } while (0)
#define TERMINATE_ERROR(_x,s,m,suff)	\
	do { TermError t(_x,s,m,suff); (_x)->terminateWith(t, s); } while (0)
#define TERMINATE_ERRORV(_x,s,m,suff,h,v) do {		\
		limited_sstream	ss(16*1024);		\
		ss << h << v << '\n';			\
		TERMINATE_ERROR_LONG(_x,s,m,suff,ss.str(),false); } while (0)

#define REPORT_ERROR(_x, s, m, suff) do {	\
	/* ExecutionState	*report_es = */	\
	_x->pureFork(s, false);			\
	TERMINATE_ERROR(_x, s, m, suff);	\
} while (0)

// call error handler and terminate state, for execution errors
// (things that should not be possible, like illegal instruction or
// unlowered instrinsic, or are unsupported, like inline assembly)
#define TERMINATE_EXEC(_x, s, m)	\
	TERMINATE_ERROR_LONG(_x, s, m, "exec.err", "", false)

	// XXX should just be moved out to utility module
	ref<klee::ConstantExpr> evalConstant(llvm::Constant *c)
	{ return evalConstant(kmodule, globals.get(), c); }

	static ref<klee::ConstantExpr> evalConstant(
		const KModule* km, const Globals* gm, llvm::Constant *c);


	void executeAllocConst(
		ExecutionState &state,
		uint64_t sz,
		bool isLocal,
		KInstruction *target,
		bool zeroMemory);

	// address should be constant expr
	void executeFree(ExecutionState &state, ref<Expr> address);

	ref<klee::ConstantExpr> evalConstantExpr(llvm::ConstantExpr *ce)
	{ return evalConstantExpr(kmodule, globals.get(), ce); }

	static ref<klee::ConstantExpr> evalConstantExpr(
		const KModule* km,
		const Globals* gm,
		llvm::ConstantExpr *ce);

	void setSymbolicPathWriter(TreeStreamWriter *tsw) override
	{ symPathWriter = tsw; }
	TreeStreamWriter* getSymbolicPathWriter(void) { return symPathWriter; }

	void setReplayKTest(const struct KTest *out) override
	{ assert (0 == 1 && "Use KTestExecutor"); }
	virtual const struct KTest *getReplayKTest(void) const { return NULL; } 
	virtual bool isReplayKTest(void) const { return false; }

	/*** Runtime options ***/
	void setHaltExecution(bool v) override { haltExecution = v; }

	/*** State accessor methods ***/
	unsigned getSymbolicPathStreamID(const ExecutionState &state) override;

	bool getSymbolicSolution(
		const ExecutionState &state,
		std::vector<
			std::pair<std::string,
			std::vector<unsigned char> > > &res) override;

	void getCoveredLines(
		const ExecutionState &state,
		std::map<const std::string*, std::set<unsigned> > &res) override
	{ res = state.coveredLines; }

	StatsTracker* getStatsTracker(void) const { return statsTracker.get(); }
	StatsTracker* getStatsTracker(void) { return statsTracker.get(); }

	void yield(ExecutionState& state);

	InterpreterHandler *getInterpreterHandler(void) const
	{ return interpreterHandler; }

	bool isAtMemoryLimit(void) const { return atMemoryLimit; }

	ExeStateManager* getStateManager(void) { return stateManager; }
	void setStateManager(ExeStateManager* esm) { stateManager = esm; }

	const Forks* getForking(void) const { return forking; }
	Forks* getForking(void) { return forking; }
	void setForking(Forks* f) { forking = f; }

	ExecutionState* getCurrentState(void) const { return currentState; }
	bool hasState(const ExecutionState* es) const;

	void addTimer(std::unique_ptr<Timer> timer, double rate);
	const Globals* getGlobals(void) const { return globals.get(); }

	/* XXX XXX XXX get rid of me!! XXX XXX */
	SeedMapType	dummySeedMap;
	virtual SeedMapType& getSeedMap(void) { return dummySeedMap; }

	void setReplay(Replay* rp) override { replay = rp; }
	const Replay* getReplay(void) const { return replay; }
};

}
// End klee namespace

#endif

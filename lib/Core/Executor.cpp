//===-- Executor.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include <llvm/Support/CallSite.h>
#include <llvm/Analysis/MemoryBuiltins.h>
#include <llvm/LLVMContext.h>
#include <llvm/IntrinsicInst.h>
#include <llvm/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Target/TargetData.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/raw_ostream.h>

#include <cassert>
#include <algorithm>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

#include "static/Sugar.h"
#include "klee/Common.h"
#include "klee/ExeStateBuilder.h"
#include "klee/Expr.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/Internal/ADT/RNG.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/System/Time.h"
#include "klee/Internal/Support/Timer.h"

#include "Executor.h"
#include "ExeStateManager.h"

#include "Context.h"
#include "CoreStats.h"
#include "ImpliedValue.h"
#include "MemoryManager.h"
#include "PTree.h"
#include "Forks.h"
#include "Searcher.h"
#include "StateSolver.h"
#include "UserSearcher.h"
#include "MemUsage.h"
#include "MMU.h"
#include "BranchPredictors.h"
#include "StatsTracker.h"
#include "Globals.h"
#include "../Expr/RuleBuilder.h"

using namespace llvm;
using namespace klee;

bool	WriteTraces = false;
bool	ReplayInhibitedForks = true;
extern double	MaxSTPTime;

#define DECL_OPTBOOL(x,y)	cl::opt<bool> x(y, cl::init(false))

namespace {
  DECL_OPTBOOL(DumpSelectStack, "dump-select-stack");
  DECL_OPTBOOL(ChkConstraints, "chk-constraints");

  cl::opt<bool>
  ConcretizeEarlyTerminate(
  	"concretize-early",
	cl::desc("Concretizeearly terminations"),
	cl::init(false));

  cl::opt<bool>
  DumpBadInitValues(
  	"dump-bad-init-values",
	cl::desc("Dump states which fail to get initial values to console."),
	cl::init(false));

  cl::opt<bool>
  UsePID("use-pid",
	 cl::desc("Use proportional-integral-derivative state control"),
	 cl::init(false));

  cl::opt<bool> DumpStatesOnHalt("dump-states-on-halt", cl::init(true));

  cl::opt<bool> PreferCex("prefer-cex", cl::init(true));

  cl::opt<bool>
  DebugPrintInstructions("debug-print-instructions",
                         cl::desc("Print instructions during execution."));
  cl::opt<bool> DebugCheckForImpliedValues("debug-check-for-implied-values");

  cl::opt<bool>
  OnlyOutputStatesCoveringNew(
  	"only-output-states-covering-new", cl::init(false));

  cl::opt<bool>
  UseBranchHints(
  	"branch-hint",
  	cl::desc("Steer current state toward uncovered branches"),
	cl::init(true));

  cl::opt<bool>
  EmitAllErrors("emit-all-errors",
                cl::init(false),
                cl::desc("Generate tests cases for all errors "
                         "(default=one per (error,instruction) pair)"));

  cl::opt<double>
  MaxInstructionTime(
	"max-instruction-time",
	cl::desc("Limit single instructions to this much time (default=0 (off))"),
	cl::init(0));

  cl::opt<unsigned int>
  StopAfterNInstructions(
	"stop-after-n-instructions",
	cl::desc("Stop execution after number of instructions (0=off)"),
	cl::init(0));

  cl::opt<unsigned>
  MaxMemory("max-memory",
	cl::desc("Refuse to fork when above amount (in MB, 0=off)"),
	cl::init(0));

  // use 'external storage' because also needed by tools/klee/main.cpp
  cl::opt<bool, true>
  WriteTracesProxy("write-traces",
	cl::desc("Write .trace file for each terminated state"),
	cl::location(WriteTraces),
	cl::init(false));

  cl::opt<bool>
  IgnoreBranchConstraints(
  	"ignore-branch-constraints",
	cl::desc("Speculatively execute both sides of a symbolic branch."),
	cl::init(false));

  cl::opt<bool>
  TrackBranchExprs(
  	"track-br-exprs",
	cl::desc("Track Branching Expressions"),
	cl::init(false));

  cl::opt<bool, true>
  ReplayInhibitedForksProxy(
  	"replay-inhibited-forks",
        cl::desc("Replay fork inhibited path as new state"),
	cl::location(ReplayInhibitedForks),
	cl::init(true));

  cl::opt<bool>
  AllExternalWarnings("all-external-warnings");

  cl::opt<bool>
  UseIVC("use-ivc", cl::desc("Implied Value Concretization"), cl::init(true));

  cl::opt<bool>
  UseRuleBuilder(
  	"use-rule-builder",
	cl::desc("Machine-learned peephole expr builder"),
  	cl::init(false));

  cl::opt<bool>
  QuenchRunaways(
  	"quench-runaways",
	cl::desc("Drop states at heavily forking instructions."),
	cl::init(true));

  cl::opt<unsigned>
  SeedRNG("seed-rng", cl::desc("Seed random number generator"), cl::init(0));

}

namespace klee { RNG theRNG; }

static StateSolver* createTimerChain(double to, std::string q, std::string s);

Executor::Executor(InterpreterHandler *ih)
: kmodule(0)
, mmu(0)
, globals(0)
, interpreterHandler(ih)
, target_data(0)
, statsTracker(0)
, pathTree(0)
, symPathWriter(0)
, replayOut(0)
, replayPaths(0)
, atMemoryLimit(false)
, inhibitForking(false)
, haltExecution(false)
, onlyNonCompact(false)
, initialStateCopy(0)
, ivcEnabled(UseIVC)
, lastMemoryLimitOperationInstructions(0)
, stpTimeout(MaxInstructionTime ?
	std::min(MaxSTPTime,(double)MaxInstructionTime) : MaxSTPTime)
{
	/* rule builder should be installed before equiv checker, otherwise
	 * we wind up wasting time searching the equivdb for rules we
	 * already have! */
	if (UseRuleBuilder) {
		Expr::setBuilder(RuleBuilder::create(Expr::getBuilder()));
	}

	this->solver = createTimerChain(
		stpTimeout,
		interpreterHandler->getOutputFilename("queries.pc"),
		interpreterHandler->getOutputFilename("stp-queries.pc"));

	ObjectState::setupZeroObjs();

	memory = MemoryManager::create();
	stateManager = new ExeStateManager();
	ExecutionState::setMemoryManager(memory);
	ExeStateBuilder::replaceBuilder(new BaseExeStateBuilder());
	forking = new Forks(*this);

	if (UseBranchHints) {
		ListPredictor		*lp = new ListPredictor();
		/* unexplored condition path should have higher priority
		 * than unexplored condition value. */
		lp->add(new KBrPredictor());
		lp->add(new CondPredictor(forking));
		lp->add(new RandomPredictor());
		brPredict = lp;
	}

	if (SeedRNG) theRNG.seed(SeedRNG);
}

Executor::~Executor()
{
	if (globals) delete globals;

	std::for_each(timers.begin(), timers.end(), deleteTimerInfo);
	delete stateManager;
	if (brPredict) delete brPredict;
	if (mmu != NULL) delete mmu;
	delete memory;
	if (pathTree) delete pathTree;
	if (statsTracker) delete statsTracker;
	delete solver;
	ExeStateBuilder::replaceBuilder(NULL);
	delete forking;
}

inline void Executor::replaceStateImmForked(
	ExecutionState* os, ExecutionState* ns)
{
	stateManager->replaceStateImmediate(os, ns);
	removePTreeState(os);
}

void Executor::addConstrOrDie(ExecutionState &state, ref<Expr> condition)
{
	if (addConstraint(state, condition))
		return;

	terminateStateOnError(state, "Died adding constraint", "constr.err");
}

bool Executor::addConstraint(ExecutionState &state, ref<Expr> condition)
{
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(condition)) {
		assert(CE->isTrue() && "attempt to add invalid constraint");
		return true;
	}

	if (ChkConstraints) {
		bool	mayBeTrue, ok;

		ok = solver->mayBeTrue(state, condition, mayBeTrue);
		assert (ok);

		if (!mayBeTrue) {
			std::cerr
				<< "[CHKCON] WHOOPS: "
				<< condition << " is never true!\n";
		}
		assert (mayBeTrue);
	}

	if (!state.addConstraint(condition)) {
		std::cerr << "[CHKCON] Failed to add constraint. Expr=\n"
			<< condition << '\n';;
		return false;
	}

	if (ivcEnabled) {
		doImpliedValueConcretization(
			state,
			condition,
			ConstantExpr::alloc(1, Expr::Bool));
	}

	if (ChkConstraints) {
		bool	mustBeTrue, ok;

		ok = solver->mustBeTrue(state, condition, mustBeTrue);
		assert (ok);

		if (!mustBeTrue) {
			std::cerr
				<< "[CHKCON] WHOOPS2: "
				<< condition << " is never true!\n";
		}
		assert (mustBeTrue);
	}

	return true;
}

ref<klee::ConstantExpr> Executor::evalConstant(
	const KModule* km, const Globals* gm, Constant *c)
{
	if (llvm::ConstantExpr *ce = dyn_cast<llvm::ConstantExpr>(c))
		return evalConstantExpr(km, gm, ce);

	if (const ConstantInt *ci = dyn_cast<ConstantInt>(c))
		return ConstantExpr::alloc(ci->getValue());

	if (const ConstantFP *cf = dyn_cast<ConstantFP>(c))
		return ConstantExpr::alloc(cf->getValueAPF().bitcastToAPInt());

	if (const GlobalValue *gv = dyn_cast<GlobalValue>(c)) {
		assert (gm != NULL);
		ref<klee::ConstantExpr>	ce(gm->findAddress(gv));

		if (ce.isNull()) {
			std::cerr << "Bad global, no cookies.\n";
			std::cerr << "GV = " << (void*)gv << '\n';
			gv->dump();
		}

		assert (!ce.isNull() && "No global address!");
		return ce;
	}

	if (isa<ConstantPointerNull>(c))
		return Expr::createPointer(0);

	if (isa<UndefValue>(c) || isa<ConstantAggregateZero>(c))
		return MK_CONST(0, km->getWidthForLLVMType(c->getType()));

	if (isa<ConstantVector>(c))
		return ConstantExpr::createVector(cast<ConstantVector>(c));

	if (ConstantDataSequential *csq = dyn_cast<ConstantDataSequential>(c))
		return ConstantExpr::createSeqData(csq);

	// Constant{AggregateZero,Array,Struct,Vector}
	c->dump();
	assert(0 && "invalid argument to evalConstant()");
}

/* Concretize the given expression, and return a possible constant value.
   'reason' is documentation stating the reason for concretization. */
ref<klee::ConstantExpr>
Executor::toConstant(
	ExecutionState &state, ref<Expr> e, const char *reason,
	bool showLineInfo)
{
	ref<ConstantExpr>	value;

	e = state.constraints.simplifyExpr(e);
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(e))
		return CE;

	if (solver->getValue(state, e, value) == false) {
		terminateStateEarly(state, "toConstant query timeout");
		return MK_CONST(0, e->getWidth());
	}

	std::ostringstream	os;
	os	<< "silently concretizing (reason: " << reason
		<< ") expression " << e
		<< " to value " << value;

	if (showLineInfo) {
		os	<< " (" << (*(state.pc)).getInfo()->file << ":"
			<< (*(state.pc)).getInfo()->line << ")";
	}

	os << '\n';

	if (AllExternalWarnings)
		klee_warning(reason, os.str().c_str());
	else
		klee_warning_once(reason, "%s", os.str().c_str());

	addConstrOrDie(state, MK_EQ(e, value));

	return value;
}

void Executor::executeGetValue(
	ExecutionState &state, ref<Expr> e, KInstruction *target)
{
	ref<ConstantExpr>	value;

	if (solver->getValue(state, e, value) == false) {
		terminateStateEarly(state, "exeGetVal timeout");
		return;
	}

	if (target != NULL)
		state.bindLocal(target, value);
}

void Executor::stepInstruction(ExecutionState &state)
{
	assert (state.checkCanary() && "Not a valid state");

	if (DebugPrintInstructions) {
		raw_os_ostream	os(std::cerr);
		printFileLine(state, state.pc);
		std::cerr << std::setw(10) << stats::instructions << " ";
		os << *(state.pc->getInst()) << "\n";
	}

	if (statsTracker) statsTracker->stepInstruction(state);

	state.lastGlobalInstCount = ++stats::instructions;
	state.totalInsts++;
	state.prevPC = state.pc;
	++state.pc;

	if (stats::instructions==StopAfterNInstructions)
		haltExecution = true;
}

void Executor::executeCallNonDecl(
	ExecutionState &state,
	Function *f,
	std::vector< ref<Expr> > &arguments)
{
	// FIXME: I'm not really happy about this reliance on prevPC but it is ok, I
	// guess. This just done to avoid having to pass KInstIterator everywhere
	// instead of the actual instruction, since we can't make a KInstIterator
	// from just an instruction (unlike LLVM).
	KFunction	*kf;
	unsigned	callingArgs, funcArgs, numFormals;

	assert (!f->isDeclaration() && "Expects a non-declaration function!");
	kf = kmodule->getKFunction(f);
	assert (kf != NULL && "Executing non-shadowed function");

	state.pushFrame(state.prevPC, kf);
	state.pc = kf->instructions;

	if (statsTracker)
		statsTracker->framePushed(
			state,
			&state.stack[state.stack.size()-2]);

	// TODO: support "byval" parameter attribute
	// TODO: support zeroext, signext, sret attributes

	callingArgs = arguments.size();
	funcArgs = f->arg_size();
	if (callingArgs < funcArgs) {
		terminateStateOnError(
			state,
			"calling function with too few arguments",
			"user.err");
		return;
	}

	if (!f->isVarArg()) {
		if (callingArgs > funcArgs) {
			klee_warning_once(f, "calling %s with extra arguments.",
			f->getName().data());
		}
	} else {
		if (!state.setupCallVarArgs(funcArgs, arguments)) {
			terminateStateOnExecError(state, "out of memory (varargs)");
			return;
		}
	}

	numFormals = f->arg_size();
	for (unsigned i=0; i<numFormals; ++i)
		state.bindArgument(kf, i, arguments[i]);
}


void Executor::executeCall(
	ExecutionState &state,
	KInstruction *ki,
	Function *f,
	std::vector< ref<Expr> > &args)
{
	Function	*f2 = NULL;

	assert (f);

	if (WriteTraces)
		state.exeTraceMgr.addEvent(
			new FunctionCallTraceEvent(state, ki, f->getName()));

	if (	!f->isDeclaration() ||
		(f2 = kmodule->module->getFunction(f->getName().str())))
	{
		/* this is so that vexllvm linked modules work */
		if (f2 == NULL) f2 = f;
		if (!f2->isDeclaration()) {
			executeCallNonDecl(state, f2, args);
			return;
		}
	}

	switch(f->getIntrinsicID()) {
	// state may be destroyed by this call, cannot touch
	case Intrinsic::not_intrinsic:
		callExternalFunction(state, ki, f, args);
		break;

	// va_arg is handled by caller and intrinsic lowering, see comment for
	// ExecutionState::varargs
	case Intrinsic::vastart:  {
	StackFrame &sf = state.stack.back();
	assert(sf.varargs && "vastart called in func with no vararg obj");

	// FIXME: This is really specific to the architecture, not the pointer
	// size. This happens to work fir x86-32 and x86-64, however.
#define MMU_WORD_OP(x,y)	\
	do { MMU::MemOp	mop(	\
		true, \
		AddExpr::create(args[0], ConstantExpr::create(x, 64)),\
		y, NULL);\
	mmu->exeMemOp(state, mop); } while (0)

	Expr::Width WordSize = Context::get().getPointerWidth();
	if (WordSize == Expr::Int32) {
		MMU_WORD_OP(0, sf.varargs->getBaseExpr());
		break;
	}

	assert(WordSize == Expr::Int64 && "Unknown word size!");
	// X86-64 has quite complicated calling convention. However,
	// instead of implementing it, we can do a simple hack: just
	// make a function believe that all varargs are on stack.

	// gp offset; fp_offset; overflow_arg_area; reg_save_area
	MMU_WORD_OP(0, ConstantExpr::create(48,32));
	MMU_WORD_OP(4, ConstantExpr::create(304,32));
	MMU_WORD_OP(8, sf.varargs->getBaseExpr());
	MMU_WORD_OP(16, ConstantExpr::create(0, 64));
#undef MMU_WORD_OP
	break;
	}

	// va_end is a noop for the interpreter.
	// FIXME: We should validate that the target didn't do something bad
	// with vaeend, however (like call it twice).
	case Intrinsic::vaend: break;

	// va_copy should have been lowered.
	// FIXME: It would be nice to check for errors in the usage of this as
	// well.
	case Intrinsic::vacopy:
	default:
		klee_error("unknown intrinsic: %s", f->getName().data());
	}

	Instruction	*i(ki->getInst());
	if (InvokeInst *ii = dyn_cast<InvokeInst>(i)) {
		state.transferToBasicBlock(ii->getNormalDest(), i->getParent());
	}
}

void Executor::printFileLine(ExecutionState &state, KInstruction *ki)
{
	const InstructionInfo &ii = *ki->getInfo();
	const Function* f;

	if (!ii.file.empty()) {
		std::cerr << "     " << ii.file << ':' << ii.line << ':';
		return;
	}

	f = ki->getInst()->getParent()->getParent();
	if (f != NULL) {
		std::cerr << "     " << f->getName().str() << ':';
		return;
	}

	std::cerr << "     [no debug info]:";
}

bool Executor::isDebugIntrinsic(const Function *f)
{ return f->getIntrinsicID() == Intrinsic::dbg_declare; }

static inline const llvm::fltSemantics * fpWidthToSemantics(unsigned width)
{
	switch(width) {
	case Expr::Int32:	return &llvm::APFloat::IEEEsingle;
	case Expr::Int64:	return &llvm::APFloat::IEEEdouble;
	case Expr::Fl80:	return &llvm::APFloat::x87DoubleExtended;
	default:		return 0;
	}
}

void Executor::retFromNested(ExecutionState &state, KInstruction *ki)
{
	ReturnInst	*ri;
	KInstIterator	kcaller;
	Instruction	*caller;
	bool		isVoidReturn;
	ref<Expr>	result;

	assert (isa<ReturnInst>(ki->getInst()) && "Expected ReturnInst");

	ri = cast<ReturnInst>(ki->getInst());
	kcaller = state.getCaller();
	caller = kcaller ? kcaller->getInst() : 0;
	isVoidReturn = (ri->getNumOperands() == 0);

	assert (state.stack.size() > 1);

	if (!isVoidReturn) {
		result = eval(ki, 0, state).value;
	}

	state.popFrame();

	if (statsTracker) statsTracker->framePopped(state);

	if (InvokeInst *ii = dyn_cast<InvokeInst>(caller)) {
		state.transferToBasicBlock(ii->getNormalDest(), caller->getParent());
	} else {
		state.pc = kcaller;
		++state.pc;
	}

	if (isVoidReturn) {
		// We check that the return value has no users instead of
		// checking the type, since C defaults to returning int for
		// undeclared functions.
		if (!caller->use_empty()) {
			terminateStateOnExecError(
				state,
				"return void when caller expected a result");
		}
		return;
	}

	assert (!isVoidReturn);
	Type *t = caller->getType();
	if (t->isVoidTy())
		return;

	// may need to do coercion due to bitcasts
	assert (result.isNull() == false);
	Expr::Width from = result->getWidth();
	Expr::Width to = kmodule->getWidthForLLVMType(t);

	if (from != to) {
		CallSite	cs;

		if (isa<CallInst>(caller))
			cs = CallSite(cast<CallInst>(caller));
		else if (isa<InvokeInst>(caller))
			cs = CallSite(cast<InvokeInst>(caller));
		else {
			std::cerr << "WTF: ";
			kcaller->getInst()->dump();
		}
		// XXX need to check other param attrs ?
		if (cs.paramHasAttr(0, llvm::Attribute::SExt)) {
			result = SExtExpr::create(result, to);
		} else {
			result = ZExtExpr::create(result, to);
		}
	}

	state.bindLocal(kcaller, result);
}

const Cell& Executor::eval(
	KInstruction *ki,
	unsigned index,
	ExecutionState &state) const
{
	int vnumber;

	assert(index < ki->getInst()->getNumOperands());

	vnumber = ki->getOperand(index);
	assert(	vnumber != -1 &&
		"Invalid operand to eval(), not a value or constant!");

	// Determine if this is a constant or not.
	if (vnumber < 0) return kmodule->constantTable[-vnumber - 2];

	return state.readLocalCell(state.stack.size() - 1, vnumber);
}

void Executor::instRet(ExecutionState &state, KInstruction *ki)
{
	if (state.stack.size() <= 1) {
		assert (!(state.getCaller()) &&
			"caller set on initial stack frame");
		terminateStateOnExit(state);
		return;
	}

	retFromNested(state, ki);
}

void Executor::markBranchVisited(
	ExecutionState& state,
	KInstruction *ki,
	const StatePair& branches,
	const ref<Expr>& cond)
{
	KBrInstruction	*kbr;
	bool		isTwoWay = (branches.first && branches.second);
	bool		got_fresh, fresh, is_cond_const;

	kbr = static_cast<KBrInstruction*>(ki);

	if (statsTracker && state.getCurrentKFunc()->trackCoverage)
		statsTracker->markBranchVisited(
			kbr, branches.first, branches.second);

	if (isTwoWay)
		kbr->foundFork(state.totalInsts);

	is_cond_const = cond->getKind() == Expr::Constant;
	if (!is_cond_const) {
		if (TrackBranchExprs) {
			kbr->addExpr(cond);
		}
		kbr->seenExpr();
	}

	fresh = false;
	got_fresh = false;

	/* Mark state as representing branch if path never seen before. */
	if (branches.first != NULL) {
		if (kbr->hasFoundTrue() == false) {
			if (branches.first == &state)
				got_fresh = true;
			branches.first->setFreshBranch();
			fresh = true;
		} else if (isTwoWay)
			branches.first->setOldBranch();
		kbr->foundTrue(state.totalInsts);
	}

	if (branches.second != NULL) {
		if (kbr->hasFoundFalse() == false) {
			if (branches.second == &state)
				got_fresh = true;
			branches.second->setFreshBranch();
			fresh = true;
		} else if (isTwoWay)
			branches.second->setOldBranch();
		kbr->foundFalse(state.totalInsts);
	}

	if (branches.first == &state)
		kbr->followedTrue();
	else if (branches.second == &state)
		kbr->followedFalse();

	if (UseBranchHints && fresh && !got_fresh)
		std::cerr << "[Branch] XXX: MISSED FRESH BRANCH!!!\n";
}

void Executor::instBranch(ExecutionState& state, KInstruction* ki)
{
	BranchInst	*bi = cast<BranchInst>(ki->getInst());

	if (bi->isUnconditional()) {
		state.transferToBasicBlock(
			bi->getSuccessor(0), bi->getParent());
		return;
	}

	// FIXME: Find a way that we don't have this hidden dependency.
	assert (bi->getCondition() == bi->getOperand(0) && "Wrong op index!");
	instBranchConditional(state, ki);
}

static bool concretizeObject(
	ExecutionState		&st,
	Assignment		&a,
	const MemoryObject	*mo,
	const ObjectState	*os,
	WallTimer& wt)
{
	unsigned		sym_bytes = 0;
	ObjectState		*new_os;
	bool			all_zeroes;

	/* we only change symbolic objstates */
	if (os->isConcrete())
		return true;

	if (MaxSTPTime > 0 && wt.checkSecs() > 3*MaxSTPTime)
		return false;

	new_os = (os->getArray())
		? new ObjectState(mo->size, ARR2REF(os->getArray()))
		: new ObjectState(mo->size);

	std::cerr << "[Exe] Concretizing MO="
		<< (void*)mo->address << "--"
		<< (void*)(mo->address + mo->size) << "\n";
	all_zeroes = true;
	for (unsigned i = 0; i < os->size; i++) {
		const klee::ConstantExpr	*ce;
		ref<klee::Expr>		e;
		uint8_t			v;

		e = os->read8(i);
		ce = dyn_cast<klee::ConstantExpr>(e);
		if (ce != NULL) {
			v = ce->getZExtValue();
		} else {
			e = a.evaluateCostly(e);
			if (e.isNull() || e->getKind() != Expr::Constant) {
				delete new_os;
				return false;
			}
			ce = cast<klee::ConstantExpr>(e);
			v = ce->getZExtValue();
			sym_bytes++;
		}

		if ((sym_bytes % 200) == 199) {
			if (MaxSTPTime > 0 && wt.checkSecs() > 3*MaxSTPTime) {
				delete new_os;
				return false;
			}
			/* bump so we don't check again */
			sym_bytes++;
		}


		all_zeroes &= (v == 0);
		new_os->write8(i, v);
	}

	if (all_zeroes) {
		delete new_os;
		std::cerr << "[Exe] Concrete using zero page\n";
		new_os = ObjectState::createDemandObj(mo->size);
	}

	st.rebindObject(mo, new_os);
	return true;
}

/* branches st back into scheduler,
 * overwrites st with concrete data */
ExecutionState* Executor::concretizeState(ExecutionState& st)
{
	ExecutionState	*new_st;
	Assignment	a;

	if (st.isConcrete()) {
		std::cerr << "[Exe] Ignoring totally concretized state\n";
		return NULL;
	}

	if (bad_conc_kfuncs.count(st.getCurrentKFunc())) {
		std::cerr << "[Exe] Ignoring bad concretization kfunc\n";
		return NULL;
	}

	/* create new_st-- copy of symbolic version of st */
	new_st = forking->pureFork(st);

	/* Sweet new state. Now, make the immediate state concrete */
	std::cerr
		<< "[Exe] st=" << (void*)&st
		<< ". Concretized=" << st.concretizeCount << '\n';
	st.concretizeCount++;

	/* 1. get concretization */
	std::cerr << "[Exe] Getting assignment\n";
	if (getSatAssignment(st, a) == false) {
		bad_conc_kfuncs.insert(st.getCurrentKFunc());
		terminateStateEarly(st, "couldn't concretize imm state");
		return new_st;
	}

	/* 2. enumerate all objstates-- replace sym objstates w/ concrete */
	WallTimer	wt;
	std::cerr << "[Exe] Concretizing objstates\n";
	foreach (it, st.addressSpace.begin(), st.addressSpace.end()) {
		if (!concretizeObject(st, a, it->first, it->second.os, wt)) {
			bad_conc_kfuncs.insert(st.getCurrentKFunc());
			terminateStateEarly(st, "timeout eval on conc state");
			return new_st;
		}
	}

	/* 3. enumerate stack frames-- eval all expressions */
	std::cerr << "[Exe] Concretizing StackFrames\n";
	foreach (it, st.stackBegin(), st.stackEnd()) {
		StackFrame	&sf(*it);

		if (sf.kf == NULL)
			continue;

		/* update all registers in stack frame */
		for (unsigned i = 0; i < sf.kf->numRegisters; i++) {
			ref<Expr>	e;

			if (sf.locals[i].value.isNull())
				continue;

			e = a.evaluate(sf.locals[i].value);
			sf.locals[i].value = e;
		}
	}

	/* 4. drop constraints, since we lost all symbolics! */
	st.constraints = ConstraintManager();

	/* 5. mark symbolics as concrete */
	std::cerr << "[Exe] Set concretization\n";
	st.assignSymbolics(a);

	/* TODO: verify that assignment satisfies old constraints */
	return new_st;
}

#define RUNAWAY_REFRESH	32
static bool isRunawayBranch(KInstruction* ki)
{
	KBrInstruction	*kbr;
	double		stddevs;
	static int	count = 0;
	static double	stddev, mean, median;
	unsigned	forks, rand_mod;

	kbr = static_cast<KBrInstruction*>(ki);
	if ((count++ % RUNAWAY_REFRESH) == 0) {
		stddev = KBrInstruction::getForkStdDev();
		mean = KBrInstruction::getForkMean();
		median = KBrInstruction::getForkMedian();
	}

	if (stddev == 0)
		return false;

	forks = kbr->getForkHits();
	if (forks <= 5)
		return false;

	stddevs = ((double)(kbr->getForkHits() - mean))/stddev;
	if (stddevs < 1.0)
		return false;

	rand_mod = (1 << (1+(int)(((double)forks/(median)))));
	if ((theRNG.getInt31() % rand_mod) == 0)
		return false;

	return true;
}

void Executor::instBranchConditional(ExecutionState& state, KInstruction* ki)
{
	BranchInst	*bi = cast<BranchInst>(ki->getInst());
	const Cell	&cond = eval(ki, 0, state);
	StatePair	branches;
	bool		hasHint = false, branchHint;

	if (	QuenchRunaways &&
		cond.value->getKind() != Expr::Constant &&
		getNumStates() > 100)
	{
		state.forkDisabled = isRunawayBranch(ki);
	} else
		state.forkDisabled = false;

	if (brPredict && cond.value->getKind() != Expr::Constant)
		hasHint = brPredict->predict(
			BranchPredictor::StateBranch(state, ki, cond.value),
			branchHint);

	if (hasHint) {
		branchHint = !branchHint;
		if (branchHint) forking->setPreferTrueState(true);
		else forking->setPreferFalseState(true);
	}

	if (	IgnoreBranchConstraints &&
		cond.value->getKind() != Expr::Constant)
	{
		branches = forking->forkUnconditional(state, false);
		assert (branches.first && branches.second);
	} else {
		branches = fork(state, cond.value, false);
	}

	markBranchVisited(state, ki, branches, cond.value);

	finalizeBranch(branches.first, bi, 0 /* [0] successor => true/then */);
	finalizeBranch(branches.second, bi, 1 /* [1] successor => false/else */);

	if (hasHint) {
		if (branchHint) forking->setPreferTrueState(false);
		else forking->setPreferFalseState(false);
	}

	if (WriteTraces) {
		bool	isTwoWay = (branches.first && branches.second);
#define WRBR(x,y) \
if(x) x->exeTraceMgr.addEvent(new BranchTraceEvent(state, ki, y, isTwoWay))
		WRBR(branches.first, true);
		WRBR(branches.second, false);
#undef WRBR
	}
}

void Executor::finalizeBranch(
	ExecutionState* st,
	BranchInst*	bi,
	int branchIdx)
{
  	KFunction	*kf;

	if (st == NULL) return;

	kf = st->getCurrentKFunc();

	// reconstitute the state if it was forked into compact form but will
	// immediately cover a new instruction
	// !!! this can be done more efficiently by simply forking a regular
	// state inside fork() but that will change the fork() semantics
	if (	st->isCompact() &&
		kf->trackCoverage &&
		theStatisticManager->getIndexedValue(
			stats::uncoveredInstructions,
			kf->instructions[kf->getBasicBlockEntry(
				bi->getSuccessor(branchIdx))]->getInfo()->id))
	{
		ExecutionState *newState;
		newState = st->reconstitute(*initialStateCopy);
		replaceStateImmForked(st, newState);
		st = newState;
	}

	st->transferToBasicBlock(
		bi->getSuccessor(branchIdx),
		bi->getParent());
}

void Executor::instCall(ExecutionState& state, KInstruction *ki)
{
	CallSite cs(ki->getInst());
	unsigned numArgs = cs.arg_size();
	Function *f = cs.getCalledFunction();

	// Skip debug intrinsics, we can't evaluate their metadata arguments.
	if (f && isDebugIntrinsic(f)) return;

	// evaluate arguments
	std::vector< ref<Expr> > arguments;
	arguments.reserve(numArgs);

	for (unsigned j=0; j<numArgs; ++j)
		arguments.push_back(eval(ki, j+1, state).value);

	if (!f) {
		// special case the call with a bitcast case
		Value *fp = cs.getCalledValue();
		llvm::ConstantExpr *ce = dyn_cast<llvm::ConstantExpr>(fp);

		if (isa<InlineAsm>(fp)) {
			terminateStateOnExecError(
				state, "inline assembly is unsupported");
			return;
		}

		if (ce && ce->getOpcode()==Instruction::BitCast)
			f = executeBitCast(state, cs, ce, arguments);
	}

	if (f == NULL) {
		XferStateIter	iter;

		xferIterInit(iter, &state, ki);
		while (xferIterNext(iter))
			executeCall(*(iter.res.first), ki, iter.f, arguments);

		return;
	}

	executeCall(state, ki, f, arguments);
}

llvm::Function* Executor::executeBitCast(
	ExecutionState &state,
	CallSite&		cs,
	llvm::ConstantExpr*	ce,
	std::vector< ref<Expr> > &arguments)
{
	llvm::Function		*f;
	const FunctionType	*fType, *ceType;

	f = dyn_cast<Function>(ce->getOperand(0));
	if (f == NULL) {
		GlobalAlias*	ga;

		ga = dyn_cast<GlobalAlias>(ce->getOperand(0));
		assert (ga != NULL && "Not alias-- then what?");

		f = dyn_cast<Function>(ga->getAliasee());
		if (f == NULL) {
			llvm::ConstantExpr *new_ce;

			new_ce =  dyn_cast<llvm::ConstantExpr>(ga->getAliasee());
			if (new_ce && new_ce->getOpcode() == Instruction::BitCast)
			{
				return executeBitCast(
					state, cs, new_ce, arguments);
			}
		}
		assert (f != NULL && "Alias not function??");
	}
     	assert(f && "XXX unrecognized constant expression in call");

        fType = dyn_cast<FunctionType>(
		cast<PointerType>(f->getType())->getElementType());
	ceType = dyn_cast<FunctionType>(
		cast<PointerType>(ce->getType())->getElementType());

	assert(fType && ceType && "unable to get function type");

	// XXX check result coercion

	// XXX this really needs thought and validation
	unsigned i=0;
	for (	std::vector< ref<Expr> >::iterator
		ai = arguments.begin(), ie = arguments.end();
		ai != ie; ++ai, i++)
	{
		Expr::Width to, from;

		if (i >= fType->getNumParams()) continue;

		from = (*ai)->getWidth();
		to = kmodule->getWidthForLLVMType(fType->getParamType(i));
		if (from == to) continue;

		// XXX need to check other param attrs ?
		if (cs.paramHasAttr(i+1, llvm::Attribute::SExt)) {
			arguments[i] = SExtExpr::create(arguments[i], to);
		} else {
			arguments[i] = ZExtExpr::create(arguments[i], to);
		}
	}

	return f;
}

void Executor::instCmp(ExecutionState& state, KInstruction *ki)
{
	CmpInst*		ci = cast<CmpInst>(ki->getInst());
	Type*			op_type = ci->getOperand(1)->getType();
	ICmpInst*		ii = cast<ICmpInst>(ci);
	ICmpInst::Predicate	pred;
	VectorType*		vt;

	ref<Expr> left = eval(ki, 0, state).value;
	ref<Expr> right = eval(ki, 1, state).value;
	ref<Expr> result;

	pred = ii->getPredicate();
	if ((vt = dyn_cast<VectorType>(op_type))) {
		bool ok;
		result = cmpVector(state, pred, vt, left, right, ok);
		if (!ok) return;
	} else {
		bool ok;
		result = cmpScalar(state, pred, left, right, ok);
		if (!ok) return;
	}

	state.bindLocal(ki, result);
}

#define SETUP_VOP(x)					\
	ref<Expr>	result;				\
	unsigned int	v_elem_c;			\
	unsigned int	v_elem_w;			\
	v_elem_c = (x)->getNumElements();		\
	v_elem_w = (x)->getBitWidth() / v_elem_c;

/* FIXME: cheaper way to do this (e.g. left == right => spit out constant expr?) */
#define V_OP_APPEND(y)		V_OP(y, ConcatExpr::create(result, op_i))
#define V_OP_PREPEND(y)		V_OP(y, ConcatExpr::create(op_i, result))
#define V_OP(y,z)						\
	for (unsigned int i = 0; i < v_elem_c; i++) {		\
		ref<Expr>	left_i, right_i;		\
		ref<Expr>	op_i;				\
		left_i = MK_EXTRACT(left, i*v_elem_w, v_elem_w);\
		right_i = MK_EXTRACT(right, i*v_elem_w, v_elem_w); \
		op_i = y##Expr::create(left_i, right_i);	\
		if (i == 0) result = op_i;			\
		else result = z;				\
	}

#define SETUP_VOP_CAST(x,y)					\
	ref<Expr>	result;					\
	unsigned int	v_elem_c;				\
	unsigned int	v_elem_w_src, v_elem_w_dst;		\
	v_elem_c = (x)->getNumElements();			\
	assert (v_elem_c == (y)->getNumElements());		\
	v_elem_w_src = (x)->getBitWidth() / v_elem_c;		\
	v_elem_w_dst = (y)->getBitWidth() / v_elem_c;		\


ref<Expr> Executor::cmpVector(
	ExecutionState& state,
	int pred,
	llvm::VectorType* vt,
	ref<Expr> left, ref<Expr> right,
	bool& ok)
{
	SETUP_VOP(vt)

	ok = false;
	assert (left->getWidth() > 0);
	assert (right->getWidth() > 0);

	switch(pred) {
#define VCMP_OP(x, y) \
	case ICmpInst::x: V_OP_APPEND(y); break;

	VCMP_OP(ICMP_EQ, Eq)
	VCMP_OP(ICMP_NE, Ne)
	VCMP_OP(ICMP_UGT, Ugt)
	VCMP_OP(ICMP_UGE, Uge)
	VCMP_OP(ICMP_ULT, Ult)
	VCMP_OP(ICMP_ULE, Ule)
	VCMP_OP(ICMP_SGT, Sgt)
	VCMP_OP(ICMP_SGE, Sge)
	VCMP_OP(ICMP_SLT, Slt)
	VCMP_OP(ICMP_SLE, Sle)
	default:
	terminateStateOnExecError(state, "invalid vector ICmp predicate");
	return result;
	}
	ok = true;
	return result;
}

ref<Expr> Executor::sextVector(
	ExecutionState& state,
	ref<Expr> v,
	VectorType* srcTy,
	VectorType* dstTy)
{
	SETUP_VOP_CAST(srcTy, dstTy);
	for (unsigned int i = 0; i < v_elem_c; i++) {
		ref<Expr>	cur_elem;
		cur_elem = MK_EXTRACT(v, i*v_elem_w_src, v_elem_w_src);
		cur_elem = SExtExpr::create(cur_elem, v_elem_w_dst);
		if (i == 0)
			result = cur_elem;
		else
			result = ConcatExpr::create(result, cur_elem);
	}

	return result;
}

ref<Expr> Executor::cmpScalar(
	ExecutionState& state,
	int pred, ref<Expr> left, ref<Expr> right, bool& ok)
{
  ref<Expr> result;
  ok = false;
  switch(pred) {
  case ICmpInst::ICMP_EQ: result = EqExpr::create(left, right); break;
  case ICmpInst::ICMP_NE: result = NeExpr::create(left, right); break;
  case ICmpInst::ICMP_UGT: result = UgtExpr::create(left, right); break;
  case ICmpInst::ICMP_UGE: result = UgeExpr::create(left, right); break;
  case ICmpInst::ICMP_ULT: result = UltExpr::create(left, right); break;
  case ICmpInst::ICMP_ULE: result = UleExpr::create(left, right); break;
  case ICmpInst::ICMP_SGT: result = SgtExpr::create(left, right); break;
  case ICmpInst::ICMP_SGE: result = SgeExpr::create(left, right); break;
  case ICmpInst::ICMP_SLT: result = SltExpr::create(left, right); break;
  case ICmpInst::ICMP_SLE: result = SleExpr::create(left, right); break;
  default:
    terminateStateOnExecError(state, "invalid scalar ICmp predicate");
    return result;
  }
  ok = true;
  return result;
}

/* XXX: move to forks.c? */
void Executor::forkSwitch(
	ExecutionState& state,
	BasicBlock*	parent_bb,
	const TargetTy& defaultTarget,
	const TargetsTy& targets)
{
	StateVector			resultStates;
	std::vector<ref<Expr> >		caseConds(targets.size()+1);
	std::vector<BasicBlock*>	caseDests(targets.size()+1);
	unsigned			index;
	bool				found;

	// prepare vectors for fork call
	caseDests[0] = defaultTarget.first;
	caseConds[0] = defaultTarget.second;
	index = 1;
	foreach (mit, targets.begin(), targets.end()) {
		caseDests[index] = (*mit).second.first;
		caseConds[index] = (*mit).second.second;
		index++;
	}

	resultStates = fork(state, caseConds.size(), caseConds.data(), false);
	assert(resultStates.size() == caseConds.size());

	found = false;
	for(index = 0; index < resultStates.size(); index++) {
		ExecutionState	*es;
		BasicBlock	*destBlock;
		KFunction	*kf;
		unsigned	entry;

		es = resultStates[index];
		if (!es) continue;

		found = true;
		destBlock = caseDests[index];
		kf = state.getCurrentKFunc();

		entry = kf->getBasicBlockEntry(destBlock);
		if (	es->isCompact() &&
			kf->trackCoverage &&
			/* XXX I don't understand this condition */
			theStatisticManager->getIndexedValue(
				stats::uncoveredInstructions,
				kf->instructions[entry]->getInfo()->id))
		{
			ExecutionState *newState;
			newState = es->reconstitute(*initialStateCopy);
			replaceStateImmForked(es, newState);
			es = newState;
		}

		if (!es->isCompact())
			es->transferToBasicBlock(destBlock, parent_bb);

		// Update coverage stats
		if (	kf->trackCoverage &&
			theStatisticManager->getIndexedValue(
				stats::uncoveredInstructions,
				kf->instructions[entry]->getInfo()->id))
		{
			es->coveredNew = true;
			es->instsSinceCovNew = 1;
		}
	}

	if (!found)
		terminateState(state);
}

ref<Expr> Executor::toUnique(const ExecutionState &state, ref<Expr> &e)
{ return solver->toUnique(state, e); }

void Executor::instSwitch(ExecutionState& state, KInstruction *ki)
{
	KSwitchInstruction	*ksi(static_cast<KSwitchInstruction*>(ki));
	ref<Expr>		cond(eval(ki, 0, state).value);
	TargetTy 		defaultTarget;
	TargetsTy		targets;

	cond = toUnique(state, cond);
	ksi->orderTargets(kmodule, globals);

	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(cond)) {
		defaultTarget = ksi->getConstCondSwitchTargets(
			CE->getZExtValue(), targets);
	} else {
		defaultTarget = ksi->getExprCondSwitchTargets(cond, targets);
	}

	/* may not have any targets to jump to! */
	if (targets.empty()) {
		terminateStateEarly(state, "bad switch");
		return;
	}

	forkSwitch(state, ki->getInst()->getParent(), defaultTarget, targets);
}


void Executor::instInsertElement(ExecutionState& state, KInstruction* ki)
{
	/* insert element has two parametres:
	 * 1. source vector (v)
	 * 2. element to insert
	 * 3. insertion index
	 * returns v[idx]
	 */
	ref<Expr> in_v = eval(ki, 0, state).value;
	ref<Expr> in_newelem = eval(ki, 1, state).value;
	ref<Expr> in_idx = eval(ki, 2, state).value;

	ConstantExpr* in_idx_ce = dynamic_cast<ConstantExpr*>(in_idx.get());
	assert (in_idx_ce && "NON-CONSTANT INSERT ELEMENT IDX. PUKE");
	uint64_t idx = in_idx_ce->getZExtValue();

	/* instruction has types of vectors embedded in its operands */
	InsertElementInst*	iei = cast<InsertElementInst>(ki->getInst());
	assert (iei != NULL);

	VectorType*	vt;
	vt = dyn_cast<VectorType>(iei->getOperand(0)->getType());
	unsigned int		v_elem_c = vt->getNumElements();
	unsigned int		v_elem_sz = vt->getBitWidth() / v_elem_c;

	assert (idx < v_elem_c && "InsertElement idx overflow");

	ref<Expr>	out_val;
	if (idx == (v_elem_c - 1)) {
		/* replace head; tail, head */
		out_val = ExtractExpr::create(in_v, 0, v_elem_sz*(v_elem_c-1));
		out_val = ConcatExpr::create(in_newelem, out_val);
	} else if (idx == 0) {
		/* replace tail; head, tail */
		out_val = MK_EXTRACT(in_v, v_elem_sz, v_elem_sz*(v_elem_c-1));
		out_val = ConcatExpr::create(out_val, in_newelem);
	} else {
		/* replace mid */
		/* (v, off, width) */
		out_val = ExtractExpr::create(in_v, 0, v_elem_sz*idx);

		/* head, mid */
		out_val = ConcatExpr::create(out_val, in_newelem);

		out_val = ConcatExpr::create(
			out_val,
			ExtractExpr::create(
				in_v,
				(idx+1)*v_elem_sz,
				(v_elem_c-(idx+1))*v_elem_sz) /* tail */);
	}

	assert (out_val->getWidth() == in_v->getWidth());

	state.bindLocal(ki, out_val);
}

/* NOTE: extract element instruction has two parametres:
 * 1. source vector (v)
 * 2. extraction index (idx)
 * returns v[idx]
 */
void Executor::instExtractElement(ExecutionState& state, KInstruction* ki)
{
	VectorType*	vt;
	ref<Expr>	out_val;
	ref<Expr>	in_v(eval(ki, 0, state).value);
	ref<Expr>	in_idx(eval(ki, 1, state).value);
	ConstantExpr	*in_idx_ce = dynamic_cast<ConstantExpr*>(in_idx.get());
	assert (in_idx_ce && "NON-CONSTANT EXTRACT ELEMENT IDX. PUKE");
	uint64_t	idx = in_idx_ce->getZExtValue();

	/* instruction has types of vectors embedded in its operands */
	ExtractElementInst*	eei = cast<ExtractElementInst>(ki->getInst());
	assert (eei != NULL);

	vt = dyn_cast<VectorType>(eei->getOperand(0)->getType());
	unsigned int	v_elem_c = vt->getNumElements();
	unsigned int	v_elem_sz = vt->getBitWidth() / v_elem_c;

	assert (idx < v_elem_c && "ExtrctElement idx overflow");
	out_val = ExtractExpr::create(in_v, idx * v_elem_sz, v_elem_sz);
	state.bindLocal(ki, out_val);
}

void Executor::instShuffleVector(ExecutionState& state, KInstruction* ki)
{
	/* shuffle vector has three parameters:
	 * 1. < in_vector |
	 * 2.             | in_vector >
	 * 3. < perm vect >
	 * 	Permutation vector
	 */
	ref<Expr> in_v_lo = eval(ki, 0, state).value;
	ref<Expr> in_v_hi = eval(ki, 1, state).value;
	ref<Expr> in_v_perm = eval(ki, 2, state).value;
	ConstantExpr* in_v_perm_ce = dynamic_cast<ConstantExpr*>(in_v_perm.get());
	assert (in_v_perm_ce != NULL && "WE HAVE NON-CONST SHUFFLES?? UGH.");

	/* instruction has types of vectors embedded in its operands */
	ShuffleVectorInst*	si = cast<ShuffleVectorInst>(ki->getInst());
	assert (si != NULL);
	VectorType*	vt = si->getType();
	unsigned int		v_elem_c = vt->getNumElements();
	unsigned int		v_elem_sz = vt->getBitWidth() / v_elem_c;
	unsigned int		perm_sz = in_v_perm_ce->getWidth() / v_elem_c;
	ref<Expr>		out_val;

	for (unsigned int i = 0; i < v_elem_c; i++) {
		ref<ConstantExpr>	v_idx;
		ref<Expr>		ext;
		unsigned int		idx;

		v_idx = in_v_perm_ce->Extract(i*perm_sz, perm_sz);
		idx = v_idx->getZExtValue();
		assert (idx < 2*v_elem_c && "Shuffle permutation out of range");
		if (idx < v_elem_c) {
			ext = MK_EXTRACT(in_v_lo, v_elem_sz*idx, v_elem_sz);
		} else {
			idx -= v_elem_c;
			ext = MK_EXTRACT(in_v_hi, v_elem_sz*idx, v_elem_sz);
		}

		if (i == 0) out_val = ext;
		else out_val = ConcatExpr::create(out_val, ext);
	}

	state.bindLocal(ki, out_val);
}

void Executor::instInsertValue(ExecutionState& state, KInstruction* ki)
{
	KGEPInstruction	*kgepi = dynamic_cast<KGEPInstruction*>(ki);
	int		lOffset, rOffset;
	ref<Expr>	result, l(0), r(0);
	ref<Expr>	agg = eval(ki, 0, state).value;
	ref<Expr>	val = eval(ki, 1, state).value;

	assert (kgepi != NULL);
	lOffset = kgepi->getOffsetBits()*8;
	rOffset = kgepi->getOffsetBits()*8 + val->getWidth();

	if (lOffset > 0) {
		l = MK_EXTRACT(agg, 0, lOffset);
	}

	if (rOffset < (int)agg->getWidth()) {
		r = MK_EXTRACT(agg, rOffset, agg->getWidth() - rOffset);
	}

	if (!l.isNull() && !r.isNull())
		result = MK_CONCAT(r, MK_CONCAT(val, l));
	else if (!l.isNull())
		result = MK_CONCAT(val, l);
	else if (!r.isNull())
		result = MK_CONCAT(r, val);
	else
		result = val;

	state.bindLocal(ki, result);
}

bool Executor::isFPPredicateMatched(
  APFloat::cmpResult CmpRes, CmpInst::Predicate pred)
{
  switch(pred) {
  // Predicates which only care about whether or not the operands are NaNs.
  case FCmpInst::FCMP_ORD: return CmpRes != APFloat::cmpUnordered;
  case FCmpInst::FCMP_UNO: return CmpRes == APFloat::cmpUnordered;
  // Ordered comparisons return false if either operand is NaN.  Unordered
  // comparisons return true if either operand is NaN.
  case FCmpInst::FCMP_UEQ: return CmpRes == APFloat::cmpUnordered;
  case FCmpInst::FCMP_OEQ: return CmpRes == APFloat::cmpEqual;
  case FCmpInst::FCMP_UGT: return CmpRes == APFloat::cmpUnordered;
  case FCmpInst::FCMP_OGT: return CmpRes == APFloat::cmpGreaterThan;
  case FCmpInst::FCMP_UGE: return CmpRes == APFloat::cmpUnordered;
  case FCmpInst::FCMP_OGE:
    return CmpRes == APFloat::cmpGreaterThan || CmpRes == APFloat::cmpEqual;
  case FCmpInst::FCMP_ULT: return CmpRes == APFloat::cmpUnordered;
  case FCmpInst::FCMP_OLT: return CmpRes == APFloat::cmpLessThan;
  case FCmpInst::FCMP_ULE: return CmpRes == APFloat::cmpUnordered;
  case FCmpInst::FCMP_OLE:
    return CmpRes == APFloat::cmpLessThan || CmpRes == APFloat::cmpEqual;
  case FCmpInst::FCMP_UNE:
    return CmpRes == APFloat::cmpUnordered || CmpRes != APFloat::cmpEqual;
  case FCmpInst::FCMP_ONE:
    return CmpRes != APFloat::cmpUnordered && CmpRes != APFloat::cmpEqual;
  default: assert(0 && "Invalid FCMP predicate!");
  case FCmpInst::FCMP_FALSE: return false;
  case FCmpInst::FCMP_TRUE: return true;
  }
  return false;
}

void Executor::instAlloc(ExecutionState& state, KInstruction* ki)
{
	AllocaInst	*ai;
	Instruction	*i = ki->getInst();
	unsigned	elementSize;
	bool		isLocal;
	ref<Expr>	size;

	assert (!isMalloc(ki->getInst()) && "ANTHONY! FIX THIS");

	ai = cast<AllocaInst>(i);
	elementSize = target_data->getTypeStoreSize(ai->getAllocatedType());
	size = Expr::createPointer(elementSize);

	if (ai->isArrayAllocation()) {
		ref<Expr> count = eval(ki, 0, state).value;
		count = Expr::createCoerceToPointerType(count);
		size = MulExpr::create(size, count);
	}

	isLocal = i->getOpcode() == Instruction::Alloca;
	executeAlloc(state, size, isLocal, ki);
}

void Executor::executeInstruction(ExecutionState &state, KInstruction *ki)
{
  Instruction *i = ki->getInst();

  switch (i->getOpcode()) {

  // case Instruction::Malloc:
  case Instruction::Alloca: instAlloc(state, ki); break;

   // Control flow
  case Instruction::Ret:
	if (WriteTraces) {
		state.exeTraceMgr.addEvent(
			new FunctionReturnTraceEvent(state, ki));
	}
	instRet(state, ki);
	break;

  case Instruction::Br: instBranch(state, ki); break;
  case Instruction::Switch: instSwitch(state, ki); break;
  case Instruction::Unreachable:
    // Note that this is not necessarily an internal bug, llvm will
    // generate unreachable instructions in cases where it knows the
    // program will crash. So it is effectively a SEGV or internal
    // error.
    terminateStateOnExecError(state, "reached \"unreachable\" instruction");
    break;

  case Instruction::Invoke:
  case Instruction::Call: instCall(state, ki); break;

  case Instruction::PHI: {
    ref<Expr> result = eval(ki, state.getPHISlot(), state).value;
    state.bindLocal(ki, result);
    break;
  }

  // Special instructions
  case Instruction::Select: {
    SelectInst *SI = cast<SelectInst>(ki->getInst());
    assert(SI->getCondition() == SI->getOperand(0) &&
           "Wrong operand index!");
    ref<Expr>	cond (eval(ki, 0, state).value);
    ref<Expr>	tExpr(eval(ki, 1, state).value);
    ref<Expr>	fExpr(eval(ki, 2, state).value);
    ref<Expr>	result(SelectExpr::create(cond, tExpr, fExpr));
    state.bindLocal(ki, result);
    break;
  }

  case Instruction::VAArg:
    terminateStateOnExecError(state, "unexpected VAArg instruction");
    break;

  // Arithmetic / logical
#define INST_ARITHOP(x,y)				\
  case Instruction::x : {				\
    VectorType*	vt;				\
    ref<Expr> left = eval(ki, 0, state).value;		\
    ref<Expr> right = eval(ki, 1, state).value;		\
    vt = dyn_cast<VectorType>(ki->getInst()->getOperand(0)->getType()); \
    if (vt) { 				\
	SETUP_VOP(vt);			\
	V_OP_PREPEND(x);		\
	state.bindLocal(ki, result);	\
	break;				\
    }					\
    state.bindLocal(ki, y::create(left, right));     \
    break; }

#define INST_DIVOP(x,y)						\
	case Instruction::x : {					\
	VectorType		*vt;				\
	ExecutionState	*ok_state, *bad_state;			\
	ref<Expr>	left(eval(ki, 0, state).value);		\
	ref<Expr>	right(eval(ki, 1, state).value);	\
	bad_state = ok_state = NULL;				\
	if (!isa<ConstantExpr>(right)) {			\
		StatePair	sp = fork(			\
			state,					\
			MK_EQ(right, MK_CONST(0, right->getWidth())), \
			true);					\
		bad_state = sp.first;				\
		ok_state = sp.second;				\
	} else if (right->isZero()) {				\
		bad_state = &state;				\
	} else {						\
		ok_state = &state;				\
	}							\
	if (bad_state != NULL) {				\
		terminateStateOnError(*bad_state, 		\
			"Tried to divide by zero!",		\
			"div.err");				\
	}							\
	if (ok_state == NULL) break;				\
	vt = dyn_cast<VectorType>(ki->getInst()->getOperand(0)->getType()); \
	if (vt) { 						\
		SETUP_VOP(vt);					\
		V_OP_PREPEND(x);				\
		ok_state->bindLocal(ki, result);		\
		break;						\
	}							\
	ok_state->bindLocal(ki, y::create(left, right));	\
	break; }

  INST_ARITHOP(Add,AddExpr)
  INST_ARITHOP(Sub,SubExpr)
  INST_ARITHOP(Mul,MulExpr)
  INST_DIVOP(UDiv,UDivExpr)
  INST_DIVOP(SDiv,SDivExpr)
  INST_DIVOP(URem,URemExpr)
  INST_DIVOP(SRem,SRemExpr)
  INST_ARITHOP(And,AndExpr)
  INST_ARITHOP(Or,OrExpr)
  INST_ARITHOP(Xor,XorExpr)
  INST_ARITHOP(Shl,ShlExpr)
  INST_ARITHOP(LShr,LShrExpr)
  INST_ARITHOP(AShr,AShrExpr)

  case Instruction::ICmp: instCmp(state, ki); break;

  case Instruction::Load: {
	ref<Expr> 	base(eval(ki, 0, state).value);
	MMU::MemOp	mop(false, base, 0, ki);
	mmu->exeMemOp(state, mop);
	break;
  }
  case Instruction::Store: {
	ref<Expr>	base(eval(ki, 1, state).value);
	ref<Expr>	value(eval(ki, 0, state).value);
	MMU::MemOp	mop(true, base, value, 0);
	mmu->exeMemOp(state, mop);
	break;
  }

  case Instruction::GetElementPtr: instGetElementPtr(state, ki); break;

    // Conversion
  case Instruction::Trunc: {
    CastInst *ci = cast<CastInst>(i);
    ref<Expr> result = ExtractExpr::create(
      eval(ki, 0, state).value,
      0,
      kmodule->getWidthForLLVMType(ci->getType()));

    state.bindLocal(ki, result);
    break;
  }
  case Instruction::ZExt: {
    CastInst *ci = cast<CastInst>(i);
    ref<Expr> result = ZExtExpr::create(
      eval(ki, 0, state).value,
      kmodule->getWidthForLLVMType(ci->getType()));

    state.bindLocal(ki, result);
    break;
  }
  case Instruction::SExt: {
    CastInst 		*ci = cast<CastInst>(i);
    VectorType		*vt_src, *vt_dst;
    ref<Expr>		result, evaled;

    vt_src = dyn_cast<VectorType>(ci->getSrcTy());
    vt_dst = dyn_cast<VectorType>(ci->getDestTy());
    evaled =  eval(ki, 0, state).value;
    if (vt_src) {
      result = sextVector(state, evaled, vt_src, vt_dst);
    } else {
      result = SExtExpr::create(
        evaled,
        kmodule->getWidthForLLVMType(ci->getType()));
    }
    state.bindLocal(ki, result);
    break;
  }

  case Instruction::PtrToInt:
  case Instruction::IntToPtr: {
    CastInst *ci = cast<CastInst>(i);
    Expr::Width cType = kmodule->getWidthForLLVMType(ci->getType());
    ref<Expr> arg = eval(ki, 0, state).value;
    state.bindLocal(ki, ZExtExpr::create(arg, cType));
    break;
  }

  case Instruction::BitCast:
    state.bindLocal(ki, eval(ki, 0, state).value);
    break;

    // Floating point arith instructions
#define INST_FOP_ARITH(x,y)					\
  case Instruction::x: {					\
    ref<ConstantExpr> left, right;				\
    right = toConstant(state, eval(ki, 1, state).value, "floating point");	\
    left = toConstant(state, eval(ki, 0, state).value, "floating point");	\
    if (!fpWidthToSemantics(left->getWidth()) ||				\
        !fpWidthToSemantics(right->getWidth()))					\
      return terminateStateOnExecError(state, "Unsupported "#x" operation");	\
	\
    llvm::APFloat Res(left->getAPValue());					\
    Res.y(APFloat(right->getAPValue()), APFloat::rmNearestTiesToEven);		\
    state.bindLocal(ki, ConstantExpr::alloc(Res.bitcastToAPInt()));		\
    break; }

INST_FOP_ARITH(FAdd, add)
INST_FOP_ARITH(FSub, subtract)
INST_FOP_ARITH(FMul, multiply)
INST_FOP_ARITH(FDiv, divide)
INST_FOP_ARITH(FRem, mod)

  case Instruction::FPTrunc: {
    FPTruncInst *fi = cast<FPTruncInst>(i);
    Expr::Width resultType = kmodule->getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    if (!fpWidthToSemantics(arg->getWidth()) || resultType > arg->getWidth())
      return terminateStateOnExecError(state, "Unsupported FPTrunc operation");

    llvm::APFloat Res(arg->getAPValue());
    bool losesInfo = false;
    Res.convert(*fpWidthToSemantics(resultType),
                llvm::APFloat::rmNearestTiesToEven,
                &losesInfo);
    state.bindLocal(ki, ConstantExpr::alloc(Res));
    break;
  }

  case Instruction::FPExt: {
    FPExtInst *fi = cast<FPExtInst>(i);
    Expr::Width resultType = kmodule->getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    if (!fpWidthToSemantics(arg->getWidth()) || arg->getWidth() > resultType)
      return terminateStateOnExecError(state, "Unsupported FPExt operation");

    llvm::APFloat Res(arg->getAPValue());
    bool losesInfo = false;
    Res.convert(*fpWidthToSemantics(resultType),
                llvm::APFloat::rmNearestTiesToEven,
                &losesInfo);
    state.bindLocal(ki, ConstantExpr::alloc(Res));
    break;
  }

  case Instruction::FPToUI: {
    FPToUIInst *fi = cast<FPToUIInst>(i);
    Expr::Width resultType = kmodule->getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                       "floating point");
    if (!fpWidthToSemantics(arg->getWidth()) || resultType > 64)
      return terminateStateOnExecError(state, "Unsupported FPToUI operation");

    llvm::APFloat Arg(arg->getAPValue());
    uint64_t value = 0;
    bool isExact = true;
    Arg.convertToInteger(&value, resultType, false,
                         llvm::APFloat::rmTowardZero, &isExact);
    state.bindLocal(ki, ConstantExpr::alloc(value, resultType));
    break;
  }

  case Instruction::FPToSI: {
    FPToSIInst *fi = cast<FPToSIInst>(i);
    Expr::Width resultType = kmodule->getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                       "floating point");
    if (!fpWidthToSemantics(arg->getWidth()) || resultType > 64)
      return terminateStateOnExecError(state, "Unsupported FPToSI operation");

    llvm::APFloat Arg(arg->getAPValue());
    uint64_t value = 0;
    bool isExact = true;
    Arg.convertToInteger(&value, resultType, false,
                         llvm::APFloat::rmTowardZero, &isExact);
    state.bindLocal(ki, ConstantExpr::alloc(value, resultType));
    break;
  }

  case Instruction::UIToFP: {
    UIToFPInst *fi = cast<UIToFPInst>(i);
    Expr::Width resultType = kmodule->getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                       "floating point");
    const llvm::fltSemantics *semantics = fpWidthToSemantics(resultType);
    if (!semantics)
      return terminateStateOnExecError(state, "Unsupported UIToFP operation");
    llvm::APFloat f(*semantics, 0);
    f.convertFromAPInt(arg->getAPValue(), false,
                       llvm::APFloat::rmNearestTiesToEven);

    state.bindLocal(ki, ConstantExpr::alloc(f));
    break;
  }

  case Instruction::SIToFP: {
    SIToFPInst *fi = cast<SIToFPInst>(i);
    Expr::Width resultType = kmodule->getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                       "floating point");
    const llvm::fltSemantics *semantics = fpWidthToSemantics(resultType);
    if (!semantics)
      return terminateStateOnExecError(state, "Unsupported SIToFP operation");
    llvm::APFloat f(*semantics, 0);
    f.convertFromAPInt(arg->getAPValue(), true,
                       llvm::APFloat::rmNearestTiesToEven);

    state.bindLocal(ki, ConstantExpr::alloc(f));
    break;
  }

  case Instruction::FCmp: {
    FCmpInst *fi = cast<FCmpInst>(i);
    ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value,
                                         "floating point");
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FCmp operation");

    APFloat LHS(left->getAPValue());
    APFloat RHS(right->getAPValue());
    APFloat::cmpResult CmpRes = LHS.compare(RHS);

    state.bindLocal(ki,
      ConstantExpr::alloc(
        isFPPredicateMatched(CmpRes, fi->getPredicate()),
        Expr::Bool));
    break;
  }

  case Instruction::InsertValue: {
	instInsertValue(state, ki);
    break;
  }

  case Instruction::ExtractValue: {
    KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(ki);
    ref<Expr> agg, result;
    agg = eval(ki, 0, state).value;
    result = ExtractExpr::create(
      agg, kgepi->getOffsetBits()*8, kmodule->getWidthForLLVMType(i->getType()));

    state.bindLocal(ki, result);
    break;
  }


  // Vector instructions...
  case Instruction::ExtractElement: instExtractElement(state, ki); break;
  case Instruction::InsertElement:  instInsertElement(state, ki); break;
  case Instruction::ShuffleVector:  instShuffleVector(state, ki); break;

  default:
    if (isMalloc(i)) {
      instAlloc(state, ki);
      break;
    } else if (isFreeCall(i)) {
      executeFree(state, eval(ki, 0, state).value);
      break;
    }

    std::cerr << "OOPS! ";
    i->dump();
    terminateStateOnExecError(state, "illegal instruction");
    break;
  }

	if (Expr::errors) {
		std::stringstream	ss;
		ss << "expr error\n";
		if (Expr::errorMsg.empty() == false)
			ss << "error msg: " << Expr::errorMsg << '\n';
		if (!Expr::errorExpr.isNull())
			ss << "bad expr: " << Expr::errorExpr << '\n';
		terminateStateOnError(state, ss.str(), "expr.err");
		Expr::resetErrors();
	}
}

void Executor::removePTreeState(
	ExecutionState* es,
	ExecutionState** root_to_be_removed)
{
	ExecutionState	*root;
	root = pathTree->removeState(stateManager, es);
	if (root != NULL) *root_to_be_removed = root;
}

void Executor::removeRoot(ExecutionState* es)
{ pathTree->removeRoot(stateManager, es); }

// guess at how many to kill
void Executor::killStates(ExecutionState* &state)
{
  uint64_t numStates = stateManager->size();
  uint64_t mbs = getMemUsageMB();
  unsigned toKill = std::max((uint64_t)1, numStates - (numStates*MaxMemory)/mbs);
  assert (mbs > MaxMemory);

  klee_warning("killing %u states (over mem). Total: %ld.", toKill, numStates);

  std::vector<ExecutionState*> arr(stateManager->begin(), stateManager->end());

  // use priority ordering for selecting which states to kill
  std::partial_sort(
    arr.begin(), arr.begin() + toKill, arr.end(), KillOrCompactOrdering());
  for (unsigned i = 0; i < toKill; ++i) {
    terminateStateEarly(*arr[i], "memory limit");
    if (state == arr[i]) state = NULL;
  }
  klee_message("Killed %u states.", toKill);
}

void Executor::stepStateInst(ExecutionState* &state)
{
	assert (state->checkCanary() && "Not a valid state");

	KInstruction *ki = state->pc;
	assert(ki);

	stepInstruction(*state);
	executeInstruction(*state, ki);
	processTimers(state, MaxInstructionTime);
}

void Executor::handleMemoryUtilization(ExecutionState* &state)
{
	uint64_t mbs;

	if (!(MaxMemory && (stats::instructions & 0xFFFF) == 0))
		return;

	// Avoid calling GetMallocUsage() often; it is O(elts on freelist).
	if (UsePID && MaxMemory) {
		handleMemoryPID(state);
		return;
	}

	mbs = getMemUsageMB();
	if (mbs < 0.9*MaxMemory) {
		atMemoryLimit = false;
		return;
	}

	if (mbs <= MaxMemory) return;

	/*  (mbs > MaxMemory) */
	atMemoryLimit = true;
	onlyNonCompact = true;

	if (mbs <= MaxMemory + 100)
		return;

	/* Ran memory to the roof. FLIP OUT. */
	if 	(ReplayInhibitedForks == false ||
		/* resort to killing states if the recent compacting
		didn't help to reduce the memory usage */
		stats::instructions-
		lastMemoryLimitOperationInstructions <= 0x20000)
	{
		killStates(state);
	} else {
		stateManager->compactPressureStates(state, MaxMemory);
	}

	lastMemoryLimitOperationInstructions = stats::instructions;
}

unsigned Executor::getNumStates(void) const { return stateManager->size(); }

unsigned Executor::getNumFullStates(void) const
{ return stateManager->getNonCompactStateCount(); }

void Executor::replayPathsIntoStates(ExecutionState& initialState)
{
	assert (replayPaths);
	foreach (it, replayPaths->begin(), replayPaths->end()) {
		ExecutionState *newState;
		newState = ExecutionState::createReplay(initialState, (*it));
		pathTree->splitStates(
			newState->ptreeNode, &initialState, newState);
		stateManager->queueAdd(newState);
	}
}

void Executor::run(ExecutionState &initialState)
{
	currentState = &initialState;

	if (mmu == NULL) mmu = MMU::create(*this);

	// Delay init till now so that ticks don't accrue during
	// optimization and such.
	initTimers();

	initialStateCopy = (ReplayInhibitedForks) ? initialState.copy() : NULL;

	if (replayPaths != NULL)
		replayPathsIntoStates(initialState);

	stateManager->setInitialState(this, &initialState, replayPaths);
	stateManager->setupSearcher(this);

	runLoop();

	stateManager->teardownUserSearcher();

	if (stateManager->empty())
		goto done;

	std::cerr << "KLEE: halting execution, dumping remaining states\n";
	haltExecution = true;

	foreach (it, stateManager->begin(), stateManager->end()) {
		ExecutionState &state = **it;
		stepInstruction(state); // keep stats rolling
		if (DumpStatesOnHalt)
			terminateStateEarly(state, "execution halting");
		else
			terminateState(state);
	}
	notifyCurrent(0);

done:
	if (initialStateCopy) delete initialStateCopy;

	currentState = NULL;
	delete mmu;
	mmu = NULL;
}

void Executor::runLoop(void)
{
	ExecutionState* last_state;
	while (!stateManager->empty() && !haltExecution) {

		currentState = stateManager->selectState(!onlyNonCompact);
		if (last_state != currentState && DumpSelectStack) {
			std::cerr << "StackTrace for st="
				<< (void*)currentState
				<< ". Insts=" <<currentState->totalInsts
				<< '\n';
			printStackTrace(*currentState, std::cerr);
			std::cerr << "===================\n";
		}

		assert (currentState != NULL &&
			"State man not empty, but selectState is?");

		/* decompress state if compact */
		if (currentState->isCompact()) {
			ExecutionState* newSt;

			assert (initialStateCopy != NULL);
			newSt = currentState->reconstitute(*initialStateCopy);
			stateManager->replaceState(currentState, newSt);

			notifyCurrent(currentState);
			currentState = newSt;
		}

		stepStateInst(currentState);

		handleMemoryUtilization(currentState);
		notifyCurrent(currentState);
		last_state = currentState;
	}
}

void Executor::notifyCurrent(ExecutionState* current)
{
	stateManager->commitQueue(this, current);
	if (	stateManager->getNonCompactStateCount() == 0
		&& !stateManager->empty())
	{
		onlyNonCompact = false;
	}
}

std::string Executor::getAddressInfo(
	ExecutionState &state,
	ref<Expr> address) const
{
	std::ostringstream	info;
	uint64_t		example;

	info << "\taddress: ";
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(address)) {
		example = CE->getZExtValue();
		info << (void*)example << "\n";
	} else {
		std::pair< ref<Expr>, ref<Expr> > res;
		ref<ConstantExpr>       value;

		info << address << "\n";
		if (solver->getValue(state, address, value) == false) {
			info << "\texample: ???\n";
			return info.str();
		}

		example = value->getZExtValue();
		info << "\texample: " << example << "\n";
		// XXX: It turns out that getRange() is really slow
		// Nothing really uses this information,
		// but it grinds things to a halt when there is a memory error.
#if 0
		if (solver->getRange(state, address, res) == false)
			return info.str();
		info << "\trange: [" << res.first << ", " << res.second <<"]\n";
#endif
	}

	state.addressSpace.printAddressInfo(info, example);

	return info.str();
}

void Executor::yield(ExecutionState& state)
{
	terminateStateOnError(state, "yielding state", "yield");
	// stateManager->yield(&state);
}

void Executor::terminateState(ExecutionState &state)
{
	if (replayOut && replayPosition!=replayOut->numObjects) {
		klee_warning_once(
			replayOut,
			"replay did not consume all objects in test input.");
	}

	interpreterHandler->incPathsExplored();

	if (!stateManager->isAddedState(&state)) {
		state.pc = state.prevPC;
		stateManager->queueRemove(&state);
		return;
	}

	stateManager->dropAdded(&state);
	pathTree->remove(state.ptreeNode);
	delete &state;
}

void Executor::terminateStateEarly(
	ExecutionState &state,
	const Twine &message)
{
	static int	call_depth = 0;
	ExecutionState	*term_st;

	call_depth++;
	term_st = &state;
	std::cerr << "[Exe] TERMINATING EARLY\n";
	if (	ConcretizeEarlyTerminate &&
		!haltExecution &&
		call_depth == 1 && !state.isConcrete())
	{
		ExecutionState	*sym_st;

		/* timed out on some instruction-- back it up */
		state.abortInstruction();

		sym_st = concretizeState(state);
		if (sym_st != NULL)
			term_st = sym_st;
	}

	if (isInterestingTestCase(term_st)) {
		std::stringstream	ss;

		ss << message.str() << '\n';
		printStackTrace(*term_st, ss);
		interpreterHandler->processTestCase(
			*term_st, ss.str().c_str(), "early");
	}

	call_depth--;
	terminateState(*term_st);
}

bool Executor::isInterestingTestCase(ExecutionState* st) const
{ return !OnlyOutputStatesCoveringNew || st->coveredNew; }

void Executor::terminateStateOnExit(ExecutionState &state)
{
	if (isInterestingTestCase(&state))
		interpreterHandler->processTestCase(state, 0, 0);

	terminateState(state);
}

void Executor::terminateStateOnError(
	ExecutionState &state,
	const llvm::Twine &messaget,
	const char *suffix,
	const llvm::Twine &info)
{
	std::string message = messaget.str();
	static std::set< std::pair<Instruction*, std::string> > emittedErrors;

	if (!EmitAllErrors) {
		bool	new_err;
		new_err = emittedErrors.insert(
			std::make_pair(
				state.prevPC->getInst(), message)).second;
		if (new_err == false) {
			terminateState(state);
			return;
		}
	}

	std::ostringstream msg;
	printStateErrorMessage(state, message, msg);

	std::string info_str = info.str();
	if (info_str != "") msg << "Info: \n" << info_str;

	interpreterHandler->processTestCase(state, msg.str().c_str(), suffix);

	terminateState(state);
}

void Executor::printStateErrorMessage(
	ExecutionState& state,
	const std::string& message,
	std::ostream& os)
{
	const InstructionInfo &ii = *state.prevPC->getInfo();
	if (ii.file != "") {
		klee_message("ERROR: %s:%d: %s",
			ii.file.c_str(),
			ii.line,
			message.c_str());
	} else {
		klee_message("ERROR: %s", message.c_str());
	}

	if (!EmitAllErrors)
		klee_message("NOTE: now ignoring this error at this location");

	os << "Error: " << message << "\n";
	if (ii.file != "") {
		os << "File: " << ii.file << "\n";
		os << "Line: " << ii.line << "\n";
	}

	os << "Stack: \n";
	printStackTrace(state, os);
}

void Executor::printStackTrace(ExecutionState& st, std::ostream& os) const
{ st.dumpStack(os); }

void Executor::resolveExact(
	ExecutionState &state,
	ref<Expr> p,
	ExactResolutionList &results,
	const std::string &name)
{
	// XXX we may want to be capping this?
	ResolutionList rl;
	state.addressSpace.resolve(state, solver, p, rl);

	ExecutionState *unbound = &state;
	foreach (it, rl.begin(), rl.end()) {
		ref<Expr> inBounds;

		inBounds = EqExpr::create(p, it->first->getBaseExpr());

		StatePair branches = fork(*unbound, inBounds, true);

		if (branches.first)
			results.push_back(std::make_pair(*it, branches.first));

		unbound = branches.second;
		if (!unbound) // Fork failure
			break;
	}

	if (unbound) {
		terminateStateOnError(
			*unbound,
			"memory error: invalid pointer: " + name,
			"ptr.err",
			getAddressInfo(*unbound, p));
	}
}

ObjectState* Executor::executeMakeSymbolic(
  ExecutionState &state, const MemoryObject *mo, const char* arrName)
{ return executeMakeSymbolic(state, mo, mo->getSizeExpr(), arrName); }

ObjectState* Executor::executeMakeSymbolic(
  ExecutionState &state,
  const MemoryObject *mo,
  ref<Expr> len,
  const char* arrName)
{
	if (!replayOut) return makeSymbolic(state, mo, len, arrName);
	else return makeSymbolicReplay(state, mo, len);
}

ObjectState* Executor::makeSymbolic(
	ExecutionState& state,
	const MemoryObject* mo,
	ref<Expr> len,
	const char* arrPrefix)
{
	ObjectState	*os;
	ref<Array>	array;

	array = Array::create(state.getArrName(arrPrefix), mo->mallocKey);
	array = Array::uniqueByName(array);
	os = state.bindMemObjWriteable(mo, array.get());
	state.addSymbolic(const_cast<MemoryObject*>(mo) /* yuck */, array.get());

	return os;
}

// Create a new object state for the memory object (instead of a copy).
ObjectState* Executor::makeSymbolicReplay(
	ExecutionState& state, const MemoryObject* mo, ref<Expr> len)
{
	ObjectState *os = state.bindMemObjWriteable(mo);
	if (replayPosition >= replayOut->numObjects) {
		terminateStateOnError(
			state, "replay count mismatch", "user.err");
		return os;
	}

	KTestObject *obj = &replayOut->objects[replayPosition++];
	if (obj->numBytes != mo->size) {
		terminateStateOnError(
			state, "replay size mismatch", "user.err");
	} else {
		for (unsigned i=0; i<mo->size; i++) {
			state.write8(os, i, obj->bytes[i]);
		}
	}

	return os;
}

unsigned Executor::getSymbolicPathStreamID(const ExecutionState &state)
{
	assert(symPathWriter != NULL);
	return state.symPathOS.getID();
}

void Executor::getSymbolicSolutionCex(
	const ExecutionState& state, ExecutionState& tmp)
{
	foreach (sym_it, state.symbolicsBegin(), state.symbolicsEnd()) {
		const MemoryObject				*mo;
		std::vector< ref<Expr> >::const_iterator	pi, pie;

		mo = sym_it->getMemoryObject();
		pi = mo->cexPreferences.begin();
		pie = mo->cexPreferences.end();

		for (; pi != pie; ++pi) {
			bool		mustBeTrue, ok;
			ref<Expr>	cond(Expr::createIsZero(*pi));
			ok = solver->mustBeTrue(tmp, cond, mustBeTrue);
			if (!ok) break;
			if (!mustBeTrue) tmp.addConstraint(*pi);
		}
		if (pi!=pie) break;
	}
}

bool Executor::getSatAssignment(const ExecutionState& st, Assignment& a)
{
	std::vector<const Array*>	objects;
	bool				ok;

	foreach (it, st.symbolicsBegin(), st.symbolicsEnd())
		objects.push_back(it->getArray());

	a = Assignment(objects);

	/* second pass to bind early concretizations */
	foreach (it, st.symbolicsBegin(), st.symbolicsEnd()) {
		const SymbolicArray		&sa(*it);
		const std::vector<uint8_t>	*conc;

		conc = sa.getConcretization();
		if (conc)
			a.bindFree(sa.getArray(), *conc);
	}


	ok = solver->getInitialValues(st, a);
	if (ok) return true;

	klee_warning("can't compute initial values (invalid constraints?)!");
	if (DumpBadInitValues)
		ExprPPrinter::printQuery(
			std::cerr,
			st.constraints,
			ConstantExpr::create(0, Expr::Bool));

	return false;
}

bool Executor::getSymbolicSolution(
	const ExecutionState &state,
	std::vector<
		std::pair<std::string,
			std::vector<unsigned char> > > &res)
{
	ExecutionState		tmp(state);
	Assignment		a;

	if (PreferCex)
		getSymbolicSolutionCex(state, tmp);

	if (!getSatAssignment(tmp, a))
		return false;

	foreach (it, state.symbolicsBegin(), state.symbolicsEnd()) {
		const std::vector<unsigned char>	*v;

		v = a.getBinding(it->getArray());
		assert (v != NULL);

		res.push_back(std::make_pair(it->getMemoryObject()->name, *v));
	}

	return true;
}

void Executor::doImpliedValueConcretization(
	ExecutionState &state,
	ref<Expr> e,
	ref<ConstantExpr> value)
{
	ImpliedValueList results;

	if (DebugCheckForImpliedValues)
		ImpliedValue::checkForImpliedValues(solver->solver, e, value);

	ImpliedValue::getImpliedValues(e, value, results);

	foreach (it, results.begin(), results.end())
		state.commitIVC(it->first, it->second);
}

void Executor::instGetElementPtr(ExecutionState& state, KInstruction *ki)
{
	KGEPInstruction		*kgepi;
	ref<Expr>		base;

	kgepi = static_cast<KGEPInstruction*>(ki);
	base = eval(ki, 0, state).value;

	foreach (it, kgepi->indices.begin(), kgepi->indices.end()) {
		uint64_t elementSize = it->second;
		ref<Expr> index = eval(ki, it->first, state).value;

		base = AddExpr::create(
			base,
			MulExpr::create(
				Expr::createCoerceToPointerType(index),
				Expr::createPointer(elementSize)));
	}

	if (kgepi->getOffsetBits())
		base = AddExpr::create(
			base,
			Expr::createPointer(kgepi->getOffsetBits()));

	state.bindLocal(ki, base);
}

void Executor::executeAllocConst(
	ExecutionState &state,
	uint64_t sz,
	bool isLocal,
	KInstruction *target,
	bool zeroMemory,
	ObjectPair reallocFrom)
{
	ObjectPair	op;
	ObjectState	*os;

	op = state.allocate(sz, isLocal, false, state.prevPC->getInst());
	if (op_mo(op) == NULL) {
		state.bindLocal(target,	Expr::createPointer(0));
		return;
	}

	os = NULL;
	if (op_os(op)->isZeroPage() == false || op_mo(reallocFrom))
		os = state.addressSpace.getWriteable(op);

	if (os) {
		if (zeroMemory)
			os->initializeToZero();
		else
			os->initializeToRandom();
	}

	state.bindLocal(target, op_mo(op)->getBaseExpr());

	if (op_mo(reallocFrom)) {
		unsigned count = std::min(op_mo(reallocFrom)->size, os->size);

		state.copy(os, op_os(reallocFrom), count);
		state.unbindObject(op_mo(reallocFrom));
	}
}

klee::ref<klee::ConstantExpr> Executor::getSmallSymAllocSize(
	ExecutionState &state, ref<Expr>& size)
{
	Expr::Width		W;
	ref<ConstantExpr>	example;
	ref<ConstantExpr>	ce_128;
	bool			ok;

	ok = solver->getValue(state, size, example);
	assert(ok && "FIXME: Unhandled solver failure");

	// start with a small example
	W = example->getWidth();
	ce_128 = ConstantExpr::alloc(128, W);
	while (example->Ugt(ce_128)->isTrue()) {
		ref<ConstantExpr>	tmp;
		bool			res;

		tmp = example->LShr(ConstantExpr::alloc(1, W));
		ok = solver->mayBeTrue(state, MK_EQ(tmp, size), res);
		assert(ok && "FIXME: Unhandled solver failure");
		if (!res)
			break;
		example = tmp;
	}

	return example;
}

void Executor::executeAllocSymbolic(
	ExecutionState &state,
	ref<Expr> size,
	bool isLocal,
	KInstruction *target,
	bool zeroMemory,
	ObjectPair reallocFrom)
{
	// XXX For now we just pick a size. Ideally we would support
	// symbolic sizes fully but even if we don't it would be better to
	// "smartly" pick a value, for example we could fork and pick the
	// min and max values and perhaps some intermediate (reasonable
	// value).
	//
	// It would also be nice to recognize the case when size has
	// exactly two values and just fork (but we need to get rid of
	// return argument first). This shows up in pcre when llvm
	// collapses the size expression with a select.
	//
	ref<ConstantExpr>	example(getSmallSymAllocSize(state, size));
	Expr::Width		W = example->getWidth();

	StatePair fixedSize = fork(state, MK_EQ(example, size), true);

	if (fixedSize.second) {
		// Check for exactly two values
		ExecutionState		*ne_state;
		ref<ConstantExpr>	tmp;
		bool			ok, res;

		ne_state = fixedSize.second;
		ok = solver->getValue(*ne_state, size, tmp);
		assert(ok && "FIXME: Unhandled solver failure");
		ok = solver->mustBeTrue(*ne_state, MK_EQ(tmp, size), res);
		assert(ok && "FIXME: Unhandled solver failure");

		if (res) {
			executeAlloc(
				*ne_state,
				tmp,
				isLocal,
				target, zeroMemory, reallocFrom);
		} else {
		// See if a *really* big value is possible. If so assume
		// malloc will fail for it, so lets fork and return 0.
			StatePair hugeSize = fork(
				*ne_state,
				MK_ULT(MK_CONST(1<<31, W), size),
				true);
			if (hugeSize.first) {
				klee_message("NOTE: found huge malloc, returing 0");
				hugeSize.first->bindLocal(
					target,
					Expr::createPointer(0));
			}

			if (hugeSize.second) {
				std::ostringstream info;
				ExprPPrinter::printOne(info, "  size expr", size);
				info << "  concretization : " << example << "\n";
				info << "  unbound example: " << tmp << "\n";
				terminateStateOnError(
					*hugeSize.second,
					"concretized symbolic size",
					"model.err",
					info.str());
			}
		}
	}

	// can be zero when fork fails
	if (fixedSize.first) {
		executeAlloc(
			*fixedSize.first,
			example,
			isLocal,
			target, zeroMemory, reallocFrom);
	}
}

void Executor::executeAlloc(
	ExecutionState &state,
	ref<Expr> size,
	bool isLocal,
	KInstruction *target,
	bool zeroMemory,
	ObjectPair reallocFrom)
{
	size = toUnique(state, size);
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(size)) {
		executeAllocConst(
			state, CE->getZExtValue(),
			isLocal, target, zeroMemory, reallocFrom);
	} else {
		executeAllocSymbolic(
			state, size, isLocal, target, zeroMemory, reallocFrom);
	}
}

void Executor::executeFree(
	ExecutionState &state,
	ref<Expr> address,
	KInstruction *target)
{
	StatePair zeroPointer = fork(state, Expr::createIsZero(address), true);

	if (zeroPointer.first && target) {
		zeroPointer.first->bindLocal(target, Expr::createPointer(0));
	}

	if (!zeroPointer.second)
		return;

	// address != 0
	ExactResolutionList rl;
	resolveExact(*zeroPointer.second, address, rl, "free");

	foreach (it, rl.begin(), rl.end()) {
		const MemoryObject *mo;

		mo = it->first.first;
		if (mo->isLocal()) {
			terminateStateOnError(
				*it->second,
				"free of alloca",
				"free.err",
				getAddressInfo(*it->second, address));
		} else if (mo->isGlobal()) {
			terminateStateOnError(
				*it->second,
				"free of global",
				"free.err",
				getAddressInfo(*it->second, address));
		} else {
			it->second->unbindObject(mo);
			if (target)
				it->second->bindLocal(
					target,
					Expr::createPointer(0));
		}
	}
}

#include <malloc.h>
void Executor::handleMemoryPID(ExecutionState* &state)
{
	#define K_P	0.6
	#define K_D	0.1	/* damping factor-- damp changes in errors */
	#define K_I	0.0001  /* systematic error-- negative while ramping  */
	unsigned	nonCompact_c;
	int		states_to_gen;
	int64_t		err;
	uint64_t	mbs;
	static int64_t	err_sum = -(int64_t)MaxMemory;
	static int64_t	last_err = 0;

	nonCompact_c = stateManager->getNonCompactStateCount();

	mbs = mallinfo().uordblks/(1024*1024);
	err = MaxMemory - mbs;

	states_to_gen = K_P*err + K_D*(err - last_err) + K_I*(err_sum);
	err_sum += err;
	last_err = err;

	if (states_to_gen < 0) {
		onlyNonCompact = false;
		stateManager->compactStates(state, -states_to_gen);
	}
}

std::string Executor::getPrettyName(llvm::Function* f) const
{ return f->getName().str(); }

ExeStateSet::const_iterator Executor::beginStates(void) const
{ return stateManager->begin(); }
ExeStateSet::const_iterator Executor::endStates(void) const
{ return stateManager->end(); }

void Executor::xferIterInit(
	struct XferStateIter& iter,
	ExecutionState* state,
	KInstruction* ki)
{
	iter.v = eval(ki, 0, *state).value;
	iter.ki = ki;
	iter.free = state;
	iter.getval_c = 0;
	iter.state_c = 0;
	iter.badjmp_c = 0;
}

#define MAX_BADJMP	10

bool Executor::xferIterNext(struct XferStateIter& iter)
{
	Function		*iter_f;
	ref<ConstantExpr>	value;

	iter_f = NULL;
	while (iter.badjmp_c < MAX_BADJMP) {
		uint64_t	addr;
		unsigned	num_funcs = kmodule->getNumKFuncs();

		if (iter.free == NULL) return false;

		if (solver->getValue(*(iter.free), iter.v, value) == false) {
			terminateStateEarly(
				*(iter.free),
				"solver died on xferIterNext");
			return false;
		}

		iter.getval_c++;
		iter.res = fork(*(iter.free), MK_EQ(iter.v, value), true);
		iter.free = iter.res.second;

		if (iter.res.first == NULL) continue;

		addr = value->getZExtValue();
		iter.state_c++;
		iter_f = getFuncByAddr(addr);

		if (iter_f == NULL) {
			if (iter.badjmp_c == 0) {
				klee_warning_once(
					(void*) (unsigned long) addr,
					"invalid function pointer: %p",
					(void*)addr);
			}

			terminateStateOnError(
				*(iter.res.first),
				"xfer iter error: bad pointer",
				"badjmp.err");
			iter.badjmp_c++;
			continue;
		}

		// Don't give warning on unique resolution
		if (iter.res.second && iter.getval_c == 1) {
			klee_warning_once(
				(void*) (unsigned long) addr,
				"resolved symbolic function pointer to: %s",
				iter_f->getName().data());
		}

		/* uncovered new function => set fresh */
		if (kmodule->getNumKFuncs() > num_funcs)
			iter.res.first->setFreshBranch();
		else if (iter.getval_c > 1)
			iter.res.first->setOldBranch();

		break;
	}

	if (iter.badjmp_c >= MAX_BADJMP) {
		terminateStateOnError(
			*(iter.free),
			"xfer iter erorr: too many bad jumps",
			"badjmp.err");
		iter.free = NULL;
		return false;
	}

	assert (iter_f != NULL && "BAD FUNCTION TO JUMP TO");
	iter.f = iter_f;

	return true;
}

Executor::StatePair Executor::fork(
	ExecutionState &current, ref<Expr> condition, bool isInternal)
{ return forking->fork(current, condition, isInternal); }

Executor::StateVector Executor::fork(
	ExecutionState &current,
	unsigned N, ref<Expr> conditions[], bool isInternal,
	bool isBranch)
{ return forking->fork(current, N, conditions, isInternal, isBranch); }


bool Executor::hasState(const ExecutionState* es) const
{
	if (getCurrentState() == es)
		return true;

	foreach (it, beginStates(), endStates())
		if ((*it) == es)
			return true;

	return false;
}

static StateSolver* createTimerChain(
	double timeout, std::string qPath, std::string logPath)
{
	Solver		*s;
	TimedSolver	*timedSolver;
	StateSolver	*ts;

	if (timeout == 0.0)
		timeout = MaxSTPTime;

	s = Solver::createChainWithTimedSolver(qPath, logPath, timedSolver);
	ts = new StateSolver(s, timedSolver);
	timedSolver->setTimeout(timeout);

	return ts;
}

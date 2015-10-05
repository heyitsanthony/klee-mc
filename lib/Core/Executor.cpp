//===-- Executor.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include <llvm/IR/CallSite.h>
#include <llvm/Analysis/MemoryBuiltins.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_os_ostream.h>

#include <cassert>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>

#include "static/Sugar.h"
#include "klee/Common.h"
#include "klee/ExeStateBuilder.h"
#include "klee/Expr.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/Internal/ADT/RNG.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/Timer.h"
#include "klee/Statistics.h"

#include "Executor.h"
#include "ExeStateManager.h"

#include "../Searcher/UserSearcher.h"

#include "SymAddrSpace.h"
#include "Context.h"
#include "CoreStats.h"
#include "ImpliedValue.h"
#include "MemoryManager.h"
#include "Forks.h"
#include "StateSolver.h"
#include "MemUsage.h"
#include "MMU.h"
#include "BranchPredictors.h"
#include "StatsTracker.h"
#include "Globals.h"
#include "SpecialFunctionHandler.h"
#include "../Expr/RuleBuilder.h"
#include "../Solver/SMTPrinter.h"
#include "PTree.h"

using namespace llvm;
using namespace klee;

bool	ReplayInhibitedForks = true;
uint32_t DebugPrintInstructions = 0;
extern double	MaxSTPTime;

namespace klee {
void PartSeedSetup(Executor* exe);
void PartSeedSetupDummy(Executor* exe);
}

#define DECL_OPTBOOL(x,y)	cl::opt<bool> x(y, cl::init(false))

namespace {
  DECL_OPTBOOL(ChkConstraints, "chk-constraints");
  DECL_OPTBOOL(YieldUncached, "yield-uncached");
  DECL_OPTBOOL(DebugPrintValues, "debug-print-values");
  DECL_OPTBOOL(DebugCheckForImpliedValues, "debug-check-for-implied-values");
  DECL_OPTBOOL(AllExternalWarnings, "all-external-warnings");
  DECL_OPTBOOL(VerifyPath, "verify-path");
  DECL_OPTBOOL(ForceCOW, "force-cow");
  DECL_OPTBOOL(DoPartialConcretize, "do-partial-conc");

  cl::opt<bool>
  DumpBadInitValues(
  	"dump-bad-init-values",
	cl::desc("Dump states which fail to get initial values to console."));

  cl::opt<bool>
  UsePID("use-pid", cl::desc("Use proportional state control"));

  cl::opt<bool> DumpStatesOnHalt("dump-states-on-halt", cl::init(true));
  DECL_OPTBOOL(PreferCex, "prefer-cex");

  cl::opt<unsigned, true>
  DebugPrintInstructionsProxy(
  	"debug-print-instructions",
	cl::location(DebugPrintInstructions),
        cl::desc("Print instructions during execution."));

  cl::opt<bool>
  UseBranchHints(
  	"branch-hint",
  	cl::desc("Steer current state toward uncovered branches"),
	cl::init(true));

  cl::opt<double>
  MaxInstructionTime(
	"max-instruction-time",
	cl::desc("Limit single instructions to this much time (default=0 (off))"),
	cl::init(0));

  cl::opt<unsigned int>
  StopAfterNInstructions(
	"stop-after-n-instructions",
	cl::desc("Stop execution after number of instructions (0=off)"));

  cl::opt<unsigned>
  MaxMemory("max-memory", cl::desc("Refuse forks above cap (in MB, 0=off)"));

  cl::opt<bool>
  IgnoreBranchConstraints(
  	"ignore-branch-constraints",
	cl::desc("Speculatively execute both sides of a symbolic branch."));

	cl::opt<bool>
	TrackBranchExprs("track-br-exprs", cl::desc("Track branching exprs."));

  cl::opt<bool, true>
  ReplayInhibitedForksProxy(
  	"replay-inhibited-forks",
        cl::desc("Replay fork inhibited path as new state"),
	cl::location(ReplayInhibitedForks),
	cl::init(true));

  cl::opt<bool>
  UseIVC("use-ivc", cl::desc("Implied Value Concretization"), cl::init(true));

  cl::opt<bool>
  UseRuleBuilder(
  	"use-rule-builder", cl::desc("Machine-learned peephole expr builder"));

	cl::opt<unsigned>
	SeedRNG("seed-rng", cl::desc("Seed random number generator"));
}

namespace klee { RNG theRNG; }

static StateSolver* createFastSolver(void);

Executor::Executor(InterpreterHandler *ih)
: kmodule(0)
, mmu(0)
, interpreterHandler(ih)
, data_layout(0)
, symPathWriter(0)
, currentState(0)
, sfh(0)
, haltExecution(false)
, replay(0)
, atMemoryLimit(false)
, inhibitForking(false)
, onlyNonCompact(false)
, initialStateCopy(0)
, ivcEnabled(UseIVC)
, lastMemoryLimitOperationInstructions(0)
, stpTimeout(MaxInstructionTime ?
	std::min(MaxSTPTime,(double)MaxInstructionTime) : MaxSTPTime)
{
	/* rule builder should be installed before equiv checker, otherwise
	 * we waste time searching the equivdb for rules we already have! */
	if (UseRuleBuilder)
		Expr::setBuilder(RuleBuilder::create(Expr::getBuilder()));

	solver = createSolverChain(
		stpTimeout,
		interpreterHandler->getOutputFilename("queries.pc"),
		interpreterHandler->getOutputFilename("stp-queries.pc"));
	fastSolver = (YieldUncached) ? createFastSolver() : NULL;

	ObjectState::setupZeroObjs();

	memory.reset(MemoryManager::create());
	stateManager = new ExeStateManager();
	ExecutionState::setMemoryManager(memory.get());
	ExeStateBuilder::replaceBuilder(new BaseExeStateBuilder());
	forking = new Forks(*this);

	if (UseBranchHints) {
		ListPredictor		*lp = new ListPredictor();
		/* unexplored path should have priority over unexp. value. */
		lp->add(new KBrPredictor());
		lp->add(new CondPredictor(forking));
		lp->add(new RandomPredictor());
		brPredict = std::unique_ptr<BranchPredictor>(lp);
	}

	if (SeedRNG) theRNG.seed(SeedRNG);
}

Executor::~Executor()
{
	globals = nullptr;

	delete sfh;
	delete replay;

	timers.clear();
	delete stateManager;
	delete mmu;

	delete fastSolver;
	delete solver;

	ExeStateBuilder::replaceBuilder(NULL);
	delete forking;
}

void Executor::replaceStateImmForked(ExecutionState* os, ExecutionState* ns)
{ stateManager->replaceStateImmediate(os, ns); }

void Executor::addConstrOrDie(ExecutionState &state, ref<Expr> condition)
{
	if (addConstraint(state, condition))
		return;

	TERMINATE_ERROR(this, state, "Died adding constraint", "constr.err");
}

bool Executor::addConstraint(ExecutionState &state, ref<Expr> cond)
{
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(cond)) {
		assert(CE->isTrue() && "attempt to add invalid constraint");
		return true;
	}

	if (ChkConstraints) {
		bool	mayBeTrue;
		WallTimer wt;

		if (solver->mayBeTrue(state, cond, mayBeTrue) == false) {
			std::cerr << "[CHK] UH0H: feasibility check on "
				<< cond << " failed.\n";
			SMTPrinter::dump(Query(
				state.constraints, cond), "prechk");
			assert (wt.checkSecs() >= MaxSTPTime && "no timeout");
		} else if (!mayBeTrue) {
			std::cerr << "[CHK] UH0H: assumption violation:"
				<< cond << "\n";
			abort();
		}
	}

	if (!state.addConstraint(cond)) {
		std::cerr << "[CHK] Failed constraint. Expr=\n" << cond << '\n';
		return false;
	}

	doImpliedValueConcretization(state, cond, MK_CONST(1, Expr::Bool));

	if (ChkConstraints) {
		bool		mustBeTrue;
		WallTimer	wt;

		if (solver->mustBeTrue(state, cond, mustBeTrue) == false) {
			std::cerr << "[CHK] UH0H: validity check on "
				<< cond << " failed.\n";
			SMTPrinter::dump(Query(
				state.constraints, cond), "postchk");
			assert (wt.checkSecs() >= MaxSTPTime && "no timeout");
		} else if (!mustBeTrue) {
			std::cerr << "[CHK] UHOH: "
				<< cond << " assumed but still contingent!\n";
			abort();
		}
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
		return MK_PTR(0);

	if (isa<UndefValue>(c) || isa<ConstantAggregateZero>(c))
		return MK_CONST(0, km->getWidthForLLVMType(c->getType()));

	if (isa<ConstantVector>(c))
		return ConstantExpr::createVector(cast<ConstantVector>(c));

	if (ConstantDataSequential *csq = dyn_cast<ConstantDataSequential>(c))
		return ConstantExpr::createSeqData(csq);

	if (ConstantArray *ca = dyn_cast<ConstantArray>(c)) {
		if (ca->getNumOperands() == 1)
			return evalConstant(km, gm, ca->getOperand(0));
	}

	// Constant{Array,Struct}
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
		TERMINATE_EARLY(this, state, "toConstant query timeout");
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
	ExecutionState &state,
	ref<Expr> e,
	KInstruction *target,
	ref<Expr> pred)
{
	ref<ConstantExpr>	value;

	if (target == NULL) return;

	if (solver->getValue(state, e, value, pred) == false) {
		TERMINATE_EARLY(this, state, "exeGetVal timeout");
		return;
	}

	state.bindLocal(target, value);
}

typedef std::map<llvm::Instruction*, std::string> inststrmap_ty;

void Executor::debugPrintInst(ExecutionState& state)
{
	static inststrmap_ty		print_cache;
	inststrmap_ty::iterator		it;
	llvm::Instruction		*ins;

	state.printFileLine();
	std::cerr << std::setw(10) << stats::instructions << " ";

	ins = state.prevPC->getInst();

	/* serializing instructions w/ LLVM is really slow as of 3.2 */
	it = print_cache.find(ins);
	if (it == print_cache.end()) {
		std::stringstream	ss;
		raw_os_ostream		os(ss);

		if (print_cache.size() > 5000)
			print_cache.clear();
		os << *(state.prevPC->getInst());
		it = print_cache.insert(std::make_pair(ins, ss.str())).first;
	}

	std::cerr << it->second << "\n";
}

void Executor::stepInstruction(ExecutionState &state)
{
	assert (state.checkCanary() && "Not a valid state");

	statsTracker->stepInstruction(state);

	state.lastGlobalInstCount = ++stats::instructions;
	state.totalInsts++;
	state.personalInsts++;
	state.prevPC = state.pc;
	++state.pc;

	if (stats::instructions == StopAfterNInstructions)
		haltExecution = true;
}

void Executor::executeCallNonDecl(
	ExecutionState &state,
	Function *f,
	std::vector< ref<Expr> > &arguments)
{
	KFunction	*kf;
	unsigned	call_arg_c, func_arg_c;

	assert (!f->isDeclaration() && "Expects a non-declaration function!");
	kf = kmodule->getKFunction(f);
	assert (kf != NULL && "Executing non-shadowed function");

	state.pushFrame(state.prevPC, kf);
	state.pc = kf->instructions;

	statsTracker->framePushed(
		state,
		&state.stack[state.stack.size()-2]);

	// TODO: support "byval", zeroext, sext, sret attributes
	call_arg_c = arguments.size();
	func_arg_c = f->arg_size();
	if (call_arg_c < func_arg_c) {
		TERMINATE_ERROR(this,
			state,
			"calling function with too few arguments",
			"user.err");
		return;
	}

	if (!f->isVarArg()) {
		if (call_arg_c > func_arg_c) {
			klee_warning_once(f, "calling %s with extra arguments.",
				f->getName().data());
		}
	} else {
		if (!state.setupCallVarArgs(func_arg_c, arguments)) {
			TERMINATE_EXEC(this, state, "out of memory (varargs)");
			return;
		}
	}

	for (unsigned i = 0; i < func_arg_c; ++i)
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

	assert (ki);

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
	//
	/* XXX: this is broken broken with the symbolic MMU code
	 * -- it limits memory dispatch to
	 *  only one symbolic access per instruction */
#define MMU_WORD_OP(x,y)	\
	do {	MMU::MemOp	mop(	\
			true, MK_ADD(args[0], MK_CONST(x, 64)), y, NULL); \
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
	MMU_WORD_OP(0, MK_CONST(48,32));
	MMU_WORD_OP(4, MK_CONST(304,32));
	MMU_WORD_OP(8, sf.varargs->getBaseExpr());
	MMU_WORD_OP(16, MK_CONST(0, 64));
#undef MMU_WORD_OP
	break;
	}

	// va_end is a noop for the interpreter.
	// FIXME: We should validate that the target didn't do something bad
	// with vaeend, however (like call it twice).
	case Intrinsic::vaend: break;

	// va_copy should have been lowered.
	// FIXME: would be nice to check for errors in usage of this as well.
	case Intrinsic::vacopy:
	default:
		klee_error("unknown intrinsic: %s", f->getName().data());
	}

	Instruction	*i(ki->getInst());
	if (InvokeInst *ii = dyn_cast<InvokeInst>(i)) {
		state.transferToBasicBlock(ii->getNormalDest(), i->getParent());
	}
}

static inline const llvm::fltSemantics * fpWidthToSemantics(unsigned width)
{
	switch(width) {
	case Expr::Int32:	return &llvm::APFloat::IEEEsingle;
	case Expr::Int64:	return &llvm::APFloat::IEEEdouble;
	case Expr::Fl80:	return &llvm::APFloat::x87DoubleExtended;
	default:		return 0; }
}

#define DECL_APF(n, c)	llvm::APFloat	n(	\
	*fpWidthToSemantics(c->getWidth()), c->getAPValue())
#define APF_FROM_CE(c)	llvm::APFloat	(	\
	*fpWidthToSemantics(c->getWidth()), c->getAPValue())

void Executor::retFromNested(ExecutionState &state, KInstruction *ki)
{
	ReturnInst	*ri;
	KInstIterator	kcaller;
	Instruction	*caller;
	Type		*t;
	bool		isVoidReturn;
	ref<Expr>	result;

	ri = dyn_cast<ReturnInst>(ki->getInst());
	assert (ri != NULL && "Expected ReturnInst");

	kcaller = state.getCaller();
	if (!kcaller) {
		/* no caller-- return to top of function. used for initlist */
		state.popFrame();
		state.pc = state.getCurrentKFunc()->instructions;
		return;
	}

	caller = kcaller->getInst();
	assert (caller != NULL);

	isVoidReturn = (ri->getNumOperands() == 0);

	assert (state.stack.size() > 1);

	if (!isVoidReturn && !(t = caller->getType())->isVoidTy()) {
		Expr::Width	from, to;

		result = (eval(ki, 0, state));
		assert (result.isNull() == false && "NO RESULT FROM EVAL");

		from = result->getWidth();
		to = kmodule->getWidthForLLVMType(t);

		// may need to do coercion due to bitcasts
		if (from != to) {
			CallSite	cs;
			bool		is_cs = true;

			if (isa<CallInst>(caller))
				cs = CallSite(cast<CallInst>(caller));
			else if (isa<InvokeInst>(caller))
				cs = CallSite(cast<InvokeInst>(caller));
			else {
				/* in case patched an arbitrary instruction with
				 * a function call (e.g. soft-mmu handlers) */
				is_cs = false;
			}

			if (is_cs) {
				// XXX need to check other param attrs ?
				result = (cs.paramHasAttr(
						0, llvm::Attribute::SExt))
					? MK_SEXT(result, to)
					: MK_ZEXT(result, to);
			}
		}
	} else if (isVoidReturn) {
		// Check return value has no users instead of checking
		// the type; C defaults to returning int for undeclared funcs.
		if (!caller->use_empty())
			TERMINATE_EXEC(this, state, "void return; wants value");
	}

	state.popFrame();

	if (InvokeInst *ii = dyn_cast<InvokeInst>(caller)) {
		state.transferToBasicBlock(ii->getNormalDest(), caller->getParent());
	} else {
		state.pc = kcaller;
		++state.pc;
	}

	/* this must come *after* pop-frame?? */
	if (result.isNull() == false)
		state.bindLocal(kcaller, result);
}

const ref<Expr> Executor::eval(
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
	if (vnumber < 0) return kmodule->constantTable[-vnumber - 2].value;

	return state.stack.getTopCell(vnumber).value;
}

void Executor::instRet(ExecutionState &state, KInstruction *ki)
{
	if (state.stack.size() > 1) {
		retFromNested(state, ki);
		return;
	}

	assert (!(state.getCaller()) && "caller set on initial stack frame");
	TERMINATE_EXIT(this, state);
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

	if (&state != branches.first && &state != branches.second)
		return;

	if (!state.stack.empty()) {
		const KFunction	*kf = state.getCurrentKFunc();
		if (kf && kf->trackCoverage)
			statsTracker->markBranchVisited(
				kbr, branches.first, branches.second);
	}

	if (isTwoWay)
		kbr->foundFork(state.totalInsts);

	is_cond_const = cond->getKind() == Expr::Constant;
	if (!is_cond_const) {
		if (TrackBranchExprs) kbr->addExpr(cond);
		kbr->seenExpr();
	}

	fresh = false;
	got_fresh = false;

	/* Mark state as representing branch if path never seen before. */
	if (branches.first != NULL) {
		if (kbr->hasFoundTrue() == false) {
			if (branches.first == &state) got_fresh = true;

			branches.first->setFreshBranch();
			fresh = true;
		} else if (isTwoWay)
			branches.first->setOldBranch();
		kbr->foundTrue(state.totalInsts);
	}

	if (branches.second != NULL) {
		if (kbr->hasFoundFalse() == false) {
			if (branches.second == &state) got_fresh = true;

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

void Executor::instBranch(ExecutionState& st, KInstruction* ki)
{
	BranchInst	*bi = cast<BranchInst>(ki->getInst());

	if (bi->isUnconditional()) {
		st.transferToBasicBlock(bi->getSuccessor(0), bi->getParent());
		return;
	}

	// FIXME: Find a way that we don't have this hidden dependency.
	assert (bi->getCondition() == bi->getOperand(0) && "Wrong op index!");
	instBranchConditional(st, ki);
}

static bool concretizeObject(
	ExecutionState		&st,
	Assignment		&a,
	const MemoryObject	*mo,
	const ObjectState	*os,
	WallTimer		&wt,
	bool			force_const = true)
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
		? ObjectState::create(mo->size, ARR2REF(os->getArray()))
		: ObjectState::create(mo->size);

	std::cerr << "[Exe] Concretizing MO="
		<< (void*)mo->address << "--"
		<< (void*)(mo->address + mo->size) << "\n";
	all_zeroes = true;
	for (unsigned i = 0; i < os->getSize(); i++) {
		const klee::ConstantExpr	*ce;
		ref<klee::Expr>		e;
		uint8_t			v;

		if ((sym_bytes % 200) == 199) {
			if (MaxSTPTime > 0 && wt.checkSecs() > 3*MaxSTPTime)
				goto err;
			/* bump so we don't check again */
			sym_bytes++;
		}

		e = os->read8(i);
		ce = dyn_cast<klee::ConstantExpr>(e);
		if (ce == NULL) {
			e = a.evaluateCostly(e);
			if (e.isNull()) goto err;
			
			if (e->getKind() != Expr::Constant) {
				if (force_const) goto err;
				v = ~0;
			} else {
				ce = cast<klee::ConstantExpr>(e);
				v = ce->getZExtValue();
				sym_bytes++;
			}
		} else
			v = ce->getZExtValue();

		all_zeroes &= (v == 0);
		new_os->write(i, e);
	}

	if (all_zeroes) {
		delete new_os;
		std::cerr << "[Exe] Concrete using zero page\n";
		new_os = ObjectState::createDemandObj(mo->size);
	}

	st.rebindObject(mo, new_os);
	return true;

err:
	delete new_os;
	return false;
}

/* branches st back into scheduler,
 * overwrites st with concrete data */
ExecutionState* Executor::concretizeState(
	ExecutionState& st, ref<Expr> bad_expr)
{
	ExecutionState	*new_st;
	Assignment	a;
	bool		do_partial;

	if (st.isConcrete()) {
		std::cerr << "[Exe] Ignoring totally concretized state\n";
		return NULL;
	}

	if (bad_conc_kfuncs.count(st.getCurrentKFunc())) {
		/* sometimes concretization takes forever on a certain
		 * codesite-- hence, we don't repeat it */
		std::cerr << "[Exe] Ignoring bad concretization kfunc\n";
		/* have separate counter for "failed" concretization? */
		st.concretizeCount++;
		return NULL;
	}

	/* create new_st-- copy of symbolic version of st */
	new_st = forking->pureFork(st);

	/* Sweet new state. Now, make the immediate state concrete */
	std::cerr
		<< "[Exe] Beginning to concretize. st=" << (void*)&st
		<< ". Concretized=" << st.concretizeCount << '\n';
	st.concretizeCount++;

	/* 1. get concretization */
	std::cerr << "[Exe] Getting assignment\n";
	if (getSatAssignment(st, a) == false) {
		bad_conc_kfuncs.insert(st.getCurrentKFunc());
		TERMINATE_EARLY(this, st, "couldn't concretize imm state");
		return new_st;
	}

	/* XXX: this doesn't quite work yet because we assume a 
	 * state is concretized upto a certain symbolic object */
	do_partial = DoPartialConcretize && !bad_expr.isNull();
	if (do_partial) {
		std::vector<const Array* >	res;
		std::set<const Array* >		arr_set, rmv_set;

		ExprUtil::findSymbolicObjects(bad_expr, res);

		for (auto arr : res) {
			std::cerr << "[Exe] Partial sym obj: "
				  << arr->name << '\n';
			arr_set.insert(arr);
		}

		for (auto &binding : a.bindings)
			if (!arr_set.count(binding.first))
				rmv_set.insert(binding.first);

		for (auto arr : rmv_set) a.resetBinding(arr);

		if (rmv_set.size() == 0) {
			std::cerr << "[Exe] No remove set. Full concretize\n";
			do_partial = false;
		} else
			a.allowFreeValues = true;
	}

	/* 2. enumerate all objstates-- replace sym objstates w/ concrete */
	WallTimer	wt;
	std::cerr << "[Exe] Concretizing objstates\n";
	for (auto &mop : st.addressSpace) {
		if (concretizeObject(
			st, a, mop.first, mop.second.os, wt, !do_partial))
			continue;

		bad_conc_kfuncs.insert(st.getCurrentKFunc());
		TERMINATE_EARLY(this, st, "timeout eval on conc state");
		return new_st;
	}

	/* 3. enumerate stack frames-- eval all expressions */
	std::cerr << "[Exe] Concretizing StackFrames\n";
	st.stack.evaluate(a);

	/* 4. mark symbolics as concrete */
	std::cerr << "[Exe] Set concretization\n";
	st.assignSymbolics(a);

	/* TODO: verify that assignment satisfies old constraints */
	return new_st;
}

void Executor::instBranchConditional(ExecutionState& state, KInstruction* ki)
{
	BranchInst	*bi = cast<BranchInst>(ki->getInst());
	ref<Expr>	cond(eval(ki, 0, state));
	bool		isConst;
	StatePair	branches;
	bool		hasHint = false, branchHint;

	isConst = cond->getKind() == Expr::Constant;
	if (brPredict && !isConst) {
		hasHint = brPredict->predict(
			BranchPredictor::StateBranch(state, ki, cond),
			branchHint);
	}

	if (YieldUncached) {
		bool mbt;
		/* XXX: queries should be passed back to fast solver stack */
		if (	state.newInsts == 0 &&
			(rand() % 3) == 0 &&	/* 1/3 chance of yielding */
			fastSolver->mayBeTrue(state, cond, mbt) == false)
		{
			state.abortInstruction();
			stateManager->yield(&state);
			return;
		}
	}

	if (hasHint) {
		branchHint = !branchHint;
		if (branchHint) forking->setPreferTrueState(true);
		else forking->setPreferFalseState(true);
	}

	if (IgnoreBranchConstraints && !isConst) {
		branches = forking->forkUnconditional(state, false);
		assert (branches.first && branches.second);
	} else {
		branches = fork(state, cond, false);
	}

	if (hasHint) {
		if (branchHint) forking->setPreferTrueState(false);
		else forking->setPreferFalseState(false);
	}

	markBranchVisited(state, ki, branches, cond);

	finalizeBranch(branches.first, bi, 0 /* [0] successor => true/then */);
	finalizeBranch(branches.second, bi, 1 /* [1] successor => false/else */);
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

	st->transferToBasicBlock(bi->getSuccessor(branchIdx), bi->getParent());
}

void Executor::instCall(ExecutionState& state, KInstruction *ki)
{
	CallSite			cs(ki->getInst());
	Function			*f = cs.getCalledFunction();
	unsigned			numArgs = cs.arg_size();
	std::vector< ref<Expr> >	args(numArgs);

	for (unsigned j = 0; j < numArgs; ++j)
		args[j] = eval(ki, j+1, state);

	if (f == NULL) {
		// special case the call with a bitcast case
		Value *fp = cs.getCalledValue();
		llvm::ConstantExpr *ce = dyn_cast<llvm::ConstantExpr>(fp);

		if (isa<InlineAsm>(fp)) {
			TERMINATE_EXEC(this, state, "inline asm not supported");
			return;
		}

		if (ce && ce->getOpcode()==Instruction::BitCast)
			f = executeBitCast(state, cs, ce, args);
	}

	if (f == NULL) {
		XferStateIter	iter;

		xferIterInit(iter, &state, ki);
		while (xferIterNext(iter))
			executeCall(*(iter.res.first), ki, iter.f, args);

		return;
	}

	executeCall(state, ki, f, args);
}

llvm::Function* Executor::executeBitCast(
	ExecutionState		&state,
	CallSite		&cs,
	llvm::ConstantExpr*	ce,
	std::vector< ref<Expr> > &args)
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

			new_ce = dyn_cast<llvm::ConstantExpr>(ga->getAliasee());
			if (new_ce && new_ce->getOpcode() == Instruction::BitCast)
				return executeBitCast(state, cs, new_ce, args);
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
		ai = args.begin(), ie = args.end();
		ai != ie; ++ai, i++)
	{
		Expr::Width to, from;

		if (i >= fType->getNumParams()) continue;

		from = (*ai)->getWidth();
		to = kmodule->getWidthForLLVMType(fType->getParamType(i));
		if (from == to) continue;

		// XXX need to check other param attrs ?
		args[i] = (cs.paramHasAttr(i+1, llvm::Attribute::SExt))
			? MK_SEXT(args[i], to)
			: MK_ZEXT(args[i], to);
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

	ref<Expr> left = eval(ki, 0, state);
	ref<Expr> right = eval(ki, 1, state);
	ref<Expr> result;

	pred = ii->getPredicate();
	result = (vt = dyn_cast<VectorType>(op_type))
		? cmpVector(state, pred, vt, left, right)
		: cmpScalar(state, pred, left, right);

	if (result.isNull()) return;

	state.bindLocal(ki, result);
}

#define SETUP_VOP(x)					\
	ref<Expr>	result;				\
	unsigned int	v_elem_c;			\
	unsigned int	v_elem_w;			\
	v_elem_c = (x)->getNumElements();		\
	v_elem_w = (x)->getBitWidth() / v_elem_c;

/* FIXME: cheaper way to do this (e.g. left == right => spit out constant expr?) */
#define V_OP(y)							\
	for (unsigned int i = 0; i < v_elem_c; i++) {		\
		ref<Expr>	left_i, right_i;		\
		ref<Expr>	op_i;				\
		left_i = MK_EXTRACT(left, i*v_elem_w, v_elem_w);\
		right_i = MK_EXTRACT(right, i*v_elem_w, v_elem_w); \
		op_i = y##Expr::create(left_i, right_i);	\
		result = (i == 0) ? op_i : MK_CONCAT(op_i, result); }

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
	ref<Expr> left, ref<Expr> right)
{
	SETUP_VOP(vt)

	assert (left->getWidth() > 0);
	assert (right->getWidth() > 0);

	switch(pred) {
#define VCMP_OP(x, y) case ICmpInst::x: V_OP(y); break;
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
		TERMINATE_EXEC(this, state, "invalid vector ICmp predicate");
		return NULL;
	}
	return result;
}

ref<Expr> Executor::extVector(
	ExecutionState& state,
	ref<Expr> v,
	VectorType* srcTy,
	VectorType* dstTy,
	bool is_zext)
{
	SETUP_VOP_CAST(srcTy, dstTy);
	for (unsigned int i = 0; i < v_elem_c; i++) {
		ref<Expr>	cur_elem;
		cur_elem = MK_EXTRACT(v, i*v_elem_w_src, v_elem_w_src);
		cur_elem = (is_zext)
			? MK_ZEXT(cur_elem, v_elem_w_dst)
			: MK_SEXT(cur_elem, v_elem_w_dst);
		result = (i == 0)
			? cur_elem
			: MK_CONCAT(cur_elem, result);
	}

	return result;
}

ref<Expr> Executor::cmpScalar(
	ExecutionState& state, int pred, ref<Expr>& left, ref<Expr>& right)
{
	switch(pred) {
	case ICmpInst::ICMP_EQ: return MK_EQ(left, right);
	case ICmpInst::ICMP_NE: return MK_NE(left, right);
	case ICmpInst::ICMP_UGT: return MK_UGT(left, right);
	case ICmpInst::ICMP_UGE: return MK_UGE(left, right);
	case ICmpInst::ICMP_ULT: return MK_ULT(left, right);
	case ICmpInst::ICMP_ULE: return MK_ULE(left, right);
	case ICmpInst::ICMP_SGT: return MK_SGT(left, right);
	case ICmpInst::ICMP_SGE: return MK_SGE(left, right);
	case ICmpInst::ICMP_SLT: return MK_SLT(left, right);
	case ICmpInst::ICMP_SLE: return MK_SLE(left, right);
	default: TERMINATE_EXEC(this, state, "invalid scalar ICmp predicate");
	}
	return NULL;
}

/* XXX: move to forks.c? */
void Executor::forkSwitch(
	ExecutionState& state,
	KInstruction* ki,
	const TargetTy& defaultTarget,
	const TargetsTy& targets)
{
	StateVector			resultStates;
	BasicBlock			*parent_bb(ki->getInst()->getParent());
	std::vector<ref<Expr> >		caseConds(targets.size()+1);
	std::vector<BasicBlock*>	caseDests(targets.size()+1);
	unsigned			index;
	bool				found;

	// prepare vectors for fork call
	caseDests[0] = defaultTarget.first;
	caseConds[0] = defaultTarget.second;
	index = 1;
	for (const auto &mit : targets) {
		caseDests[index] = mit.second.first;
		caseConds[index] = mit.second.second;
		index++;
	}

	resultStates = fork(state, caseConds, false);
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
			es->lastNewInst = state.totalInsts;
		}
	}

	if (!found)
		terminate(state);
}

ref<Expr> Executor::toUnique(const ExecutionState &state, const ref<Expr> &e)
{ return solver->toUnique(state, e); }

void Executor::instSwitch(ExecutionState& state, KInstruction *ki)
{
	KSwitchInstruction	*ksi(static_cast<KSwitchInstruction*>(ki));
	ref<Expr>		cond(eval(ki, 0, state));
	TargetTy 		defaultTarget;
	TargetsTy		targets;

	cond = toUnique(state, cond);
	ksi->orderTargets(kmodule, globals.get());

	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(cond)) {
		defaultTarget = ksi->getConstCondSwitchTargets(
			CE->getZExtValue(), targets);
	} else {
		defaultTarget = ksi->getExprCondSwitchTargets(cond, targets);
	}

	/* may not have any targets to jump to! */
	if (targets.empty()) {
		TERMINATE_EARLY(this, state, "bad switch");
		return;
	}

	forkSwitch(state, ki, defaultTarget, targets);
}

void Executor::instInsertElement(ExecutionState& state, KInstruction* ki)
{
	/* insert element has two parametres:
	 * 1. source vector (v)
	 * 2. element to insert
	 * 3. insertion index
	 * returns v[idx]
	 */
	ref<Expr> in_v = eval(ki, 0, state);
	ref<Expr> in_newelem = eval(ki, 1, state);
	ref<Expr> in_idx = eval(ki, 2, state);

	ConstantExpr* in_idx_ce = dynamic_cast<ConstantExpr*>(in_idx.get());

	/* XXX: How to handle non-constant indexes? */
	if (in_idx_ce == NULL) {
		TERMINATE_EXEC(this, state, "non-const idx on insertElement");
		klee_warning("Non-const on index for InsertElement.");
		return;
	}

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
		out_val = MK_EXTRACT(in_v, 0, v_elem_sz*(v_elem_c-1));
		out_val = MK_CONCAT(in_newelem, out_val);
	} else if (idx == 0) {
		/* replace tail; head, tail */
		out_val = MK_EXTRACT(in_v, v_elem_sz, v_elem_sz*(v_elem_c-1));
		out_val = MK_CONCAT(out_val, in_newelem);
	} else {
		/* replace mid */
		/* tail */
		out_val = MK_EXTRACT(in_v, 0, v_elem_sz*idx);

		/* mid, tail */
		out_val = MK_CONCAT(in_newelem, out_val);

		out_val = MK_CONCAT(
			MK_EXTRACT(
				in_v,
				(idx+1)*v_elem_sz,
				(v_elem_c-(idx+1))*v_elem_sz) /* head */,
			out_val);
	}

	assert (out_val->getWidth() == in_v->getWidth());
	state.bindLocal(ki, out_val);
}

/* NOTE: extract element instruction has two parametres:
 * 1. source vector (v)
 * 2. extraction index (idx)
 * returns v[idx] */
void Executor::instExtractElement(ExecutionState& state, KInstruction* ki)
{
	VectorType*	vt;
	ref<Expr>	out_val;
	ref<Expr>	in_v(eval(ki, 0, state));
	ref<Expr>	in_idx(eval(ki, 1, state));
	ConstantExpr	*in_idx_ce = dynamic_cast<ConstantExpr*>(in_idx.get());

	/* XXX: How to handle non-constant indexes? */
	if (in_idx_ce == NULL) {
		TERMINATE_EXEC(this, state, "non-const idx on extractElement");
		klee_warning("Non-const on index for ExtractElement.");
		return;
	}
	assert (in_idx_ce && "NON-CONSTANT EXTRACT ELEMENT IDX. PUKE");

	uint64_t	idx = in_idx_ce->getZExtValue();

	/* instruction has types of vectors embedded in its operands */
	ExtractElementInst*	eei = cast<ExtractElementInst>(ki->getInst());
	assert (eei != NULL);

	vt = dyn_cast<VectorType>(eei->getOperand(0)->getType());
	unsigned int	v_elem_c = vt->getNumElements();
	unsigned int	v_elem_sz = vt->getBitWidth() / v_elem_c;

	assert (idx < v_elem_c && "ExtrctElement idx overflow");
	out_val = MK_EXTRACT(in_v, idx * v_elem_sz, v_elem_sz);
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
	ref<Expr> in_v_lo(eval(ki, 0, state));
	ref<Expr> in_v_hi(eval(ki, 1, state));
	ref<Expr> in_v_perm(eval(ki, 2, state));
	ref<Expr> out_val;

	out_val = instShuffleVectorEvaled(
		cast<ShuffleVectorInst>(ki->getInst())->getType(),
		in_v_lo,
		in_v_hi,
		in_v_perm);

	state.bindLocal(ki, out_val);
}

ref<Expr> Executor::instShuffleVectorEvaled(
	VectorType		*vt,
	const ref<Expr>		&in_v_lo,
	const ref<Expr>		&in_v_hi,
	const ref<Expr>		&in_v_perm)
{
	/* instruction has types of vectors embedded in its operands */
	ref<Expr>		out_val;
	ConstantExpr* in_v_perm_ce = dynamic_cast<ConstantExpr*>(in_v_perm.get());
	assert (in_v_perm_ce != NULL && "WE HAVE NON-CONST SHUFFLES?? UGH.");
	unsigned int		v_elem_c = vt->getNumElements();
	unsigned int		v_elem_sz = vt->getBitWidth() / v_elem_c;
	unsigned int		perm_sz = in_v_perm_ce->getWidth() / v_elem_c;

#ifdef BROKEN_OSDI
	for (unsigned int i = 0; i < v_elem_c; i++) {
#else
	for (int i = v_elem_c-1; i >= 0; i--) {
#endif
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

#ifdef BROKEN_OSDI
		out_val = (i == 0)
#else
		out_val = (out_val.isNull())
#endif
			? ext
			: MK_CONCAT(out_val, ext);
	}

	return out_val;
}


void Executor::instInsertValue(ExecutionState& state, KInstruction* ki)
{
	KGEPInstruction	*kgepi = dynamic_cast<KGEPInstruction*>(ki);
	int		lOffset, rOffset;
	ref<Expr>	result, l(0), r(0);
	ref<Expr>	agg = eval(ki, 0, state);
	ref<Expr>	val = eval(ki, 1, state);

	assert (kgepi != NULL);
	lOffset = kgepi->getOffsetBits()*8;
	rOffset = kgepi->getOffsetBits()*8 + val->getWidth();

	if (lOffset > 0)
		l = MK_EXTRACT(agg, 0, lOffset);

	if (rOffset < (int)agg->getWidth())
		r = MK_EXTRACT(agg, rOffset, agg->getWidth() - rOffset);

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
	uint64_t	size_c;

	assert (!isMallocLikeFn(ki->getInst() , NULL) && "ANTHONY! FIX THIS");

	ai = cast<AllocaInst>(i);
	elementSize = data_layout->getTypeStoreSize(ai->getAllocatedType());
	size = MK_PTR(elementSize);

	if (ai->isArrayAllocation()) {
		ref<Expr> count = eval(ki, 0, state);
		count = Expr::createCoerceToPointerType(count);
		size = MK_MUL(size, count);
	}

	isLocal = i->getOpcode() == Instruction::Alloca;
	if (size->getKind() != Expr::Constant) {
		TERMINATE_EXEC(this, state, "non-const alloca");
		return;
	}

	size_c = cast<ConstantExpr>(size)->getZExtValue();

	executeAllocConst(state, size_c, isLocal, ki, true);
}

void Executor::executeInstruction(ExecutionState &state, KInstruction *ki)
{
  Instruction *i = ki->getInst();

  switch (i->getOpcode()) {

  // case Instruction::Malloc:
  case Instruction::Alloca: instAlloc(state, ki); break;

   // Control flow
  case Instruction::Ret:
	instRet(state, ki);
	break;

  case Instruction::Br: instBranch(state, ki); break;
  case Instruction::Switch: instSwitch(state, ki); break;
  case Instruction::Unreachable:
    // Note that this is not necessarily an internal bug, llvm will
    // generate unreachable instructions in cases where it knows the
    // program will crash. It is effectively a SEGV or internal error.
    TERMINATE_EXEC(this, state, "reached \"unreachable\" instruction");
    break;

  case Instruction::Invoke:
  case Instruction::Call: instCall(state, ki); break;

  case Instruction::PHI: {
    ref<Expr> result(eval(ki, state.getPHISlot(), state));
    state.bindLocal(ki, result);
    break;
  }

  // Special instructions
  case Instruction::Select: {
    SelectInst *SI = cast<SelectInst>(ki->getInst());
    assert (SI->getCondition() == SI->getOperand(0) && "Wrong operand index!");
    ref<Expr>	cond (eval(ki, 0, state));
    ref<Expr>	tExpr(eval(ki, 1, state));
    ref<Expr>	fExpr(eval(ki, 2, state));
    ref<Expr>	result(MK_SELECT(cond, tExpr, fExpr));
    state.bindLocal(ki, result);
    break;
  }

  case Instruction::VAArg:
    TERMINATE_EXEC(this, state, "unexpected VAArg instruction");
    break;

  // Arithmetic / logical
#define INST_ARITHOP(x,y)				\
  case Instruction::x : {				\
    VectorType*	vt;				\
    ref<Expr> left = eval(ki, 0, state);		\
    ref<Expr> right = eval(ki, 1, state);		\
    vt = dyn_cast<VectorType>(ki->getInst()->getOperand(0)->getType()); \
    if (vt) { 				\
	SETUP_VOP(vt);			\
	V_OP(x);			\
	state.bindLocal(ki, result);	\
	break;				\
    }					\
    state.bindLocal(ki, y::create(left, right));     \
    break; }

#define INST_DIVOP(x,y)						\
	case Instruction::x : {					\
	VectorType		*vt;				\
	ExecutionState	*ok_state, *bad_state;			\
	ref<Expr>	left(eval(ki, 0, state));		\
	ref<Expr>	right(eval(ki, 1, state));	\
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
		TERMINATE_ERROR(this, *bad_state, 		\
			"Tried to divide by zero!",		\
			"div.err");				\
	}							\
	if (ok_state == NULL) break;				\
	vt = dyn_cast<VectorType>(ki->getInst()->getOperand(0)->getType()); \
	if (vt) { 						\
		SETUP_VOP(vt);					\
		V_OP(x);					\
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
	ref<Expr> 	base(eval(ki, 0, state));
	MMU::MemOp	mop(false, base, 0, ki);
	mmu->exeMemOp(state, mop);
	break;
  }
  case Instruction::Store: {
	ref<Expr>	base(eval(ki, 1, state));
	ref<Expr>	value(eval(ki, 0, state));
	MMU::MemOp	mop(true, base, value, ki);
	mmu->exeMemOp(state, mop);
	break;
  }

  case Instruction::GetElementPtr: instGetElementPtr(state, ki); break;

    // Conversion
  case Instruction::Trunc: {
    CastInst *ci = cast<CastInst>(i);
    ref<Expr> result (MK_EXTRACT(
      eval(ki, 0, state),
      0,
      kmodule->getWidthForLLVMType(ci->getType())));

    state.bindLocal(ki, result);
    break;
  }
  case Instruction::ZExt:
  case Instruction::SExt: {
	bool is_zext = i->getOpcode() == Instruction::ZExt;
	CastInst 		*ci = cast<CastInst>(i);
	ref<Expr>		result, evaled(eval(ki, 0, state));

	auto vt_src = dyn_cast<VectorType>(ci->getSrcTy());
	auto vt_dst = dyn_cast<VectorType>(ci->getDestTy());
	result = (vt_src)
		? extVector(state, evaled, vt_src, vt_dst, is_zext)
		: ((is_zext)
			? MK_ZEXT(evaled,
				kmodule->getWidthForLLVMType(ci->getType()))
			: MK_SEXT(evaled,
				kmodule->getWidthForLLVMType(ci->getType())));
	state.bindLocal(ki, result);
	break;
  }

  case Instruction::PtrToInt:
  case Instruction::IntToPtr: {
    CastInst *ci = cast<CastInst>(i);
    Expr::Width cType = kmodule->getWidthForLLVMType(ci->getType());
    ref<Expr> arg(eval(ki, 0, state));
    state.bindLocal(ki, MK_ZEXT(arg, cType));
    break;
  }

  case Instruction::BitCast: state.bindLocal(ki, eval(ki, 0, state)); break;

    // Floating point arith instructions
#define INST_FOP_ARITH(x,y)					\
  case Instruction::x: {					\
    ref<ConstantExpr> left, right;				\
    right = toConstant(state, eval(ki, 1, state), "floating point");	\
    left = toConstant(state, eval(ki, 0, state), "floating point");	\
    if (!fpWidthToSemantics(left->getWidth()) ||				\
        !fpWidthToSemantics(right->getWidth())) {				\
      TERMINATE_EXEC(this, state, "Unsupported "#x" operation");		\
      return; } \
	\
    DECL_APF(Res, left);	\
    Res.y(APF_FROM_CE(right), APFloat::rmNearestTiesToEven);		\
    state.bindLocal(ki, ConstantExpr::alloc(Res.bitcastToAPInt()));		\
    break; }

INST_FOP_ARITH(FAdd, add)
INST_FOP_ARITH(FSub, subtract)
INST_FOP_ARITH(FMul, multiply)
INST_FOP_ARITH(FDiv, divide)
INST_FOP_ARITH(FRem, mod)

#define FP_SETUP(T,z,x,y)	\
    T *fi = cast<T>(i);	\
    Expr::Width resultType = kmodule->getWidthForLLVMType(fi->getType());	\
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state), "fp");	\
    const llvm::fltSemantics *a_semantics = fpWidthToSemantics(arg->getWidth());\
    const llvm::fltSemantics *r_semantics = fpWidthToSemantics(resultType);\
    (void)resultType, (void)a_semantics, (void)r_semantics; /* maybe unused */	\
    if (!z || x) { TERMINATE_EXEC(this, state, y); return; }

  case Instruction::FPTrunc: {
	FP_SETUP(FPTruncInst, a_semantics, resultType > arg->getWidth(),
		"Unsupported FPTrunc operation")

	DECL_APF(Res, arg);
	bool lossy = false;
	Res.convert(*r_semantics, llvm::APFloat::rmNearestTiesToEven, &lossy);
	state.bindLocal(ki, ConstantExpr::alloc(Res));
	break;
  }

  case Instruction::FPExt: {
	FP_SETUP(FPExtInst,
		a_semantics,
		arg->getWidth() > resultType,
		"Unsupported FPExt operation")

	DECL_APF(Res, arg);
	bool lossy = false;
	Res.convert(*r_semantics, llvm::APFloat::rmNearestTiesToEven, &lossy);
	state.bindLocal(ki, ConstantExpr::alloc(Res));
	break;
  }

  case Instruction::FPToUI: {
	FP_SETUP(FPToUIInst, a_semantics, resultType > 64,
		"Unsupported FPToUI operation")

	DECL_APF(Arg, arg);
	uint64_t value = 0;
	bool isExact = true;
	Arg.convertToInteger(&value, resultType, false,
			 llvm::APFloat::rmTowardZero, &isExact);
	state.bindLocal(ki, MK_CONST(value, resultType));
	break;
  }

  case Instruction::FPToSI: {
	FP_SETUP(FPToSIInst, a_semantics, resultType > 64,
		"Unsupported FPToSI operation")

	DECL_APF(Arg, arg);
	uint64_t value = 0;
	bool isExact = true;
	Arg.convertToInteger(&value, resultType, false,
			 llvm::APFloat::rmTowardZero, &isExact);
	state.bindLocal(ki, MK_CONST(value, resultType));
	break;
  }

  case Instruction::UIToFP: {
  	FP_SETUP(UIToFPInst, r_semantics, false, "Unsupported UIToFP operation");
	llvm::APFloat f(*r_semantics, 0);
	f.convertFromAPInt(
		arg->getAPValue(), false, APFloat::rmNearestTiesToEven);

	state.bindLocal(ki, ConstantExpr::alloc(f));
	break;
  }

  case Instruction::SIToFP: {
  	FP_SETUP(SIToFPInst, r_semantics, false, "Unsupported SIToFP operation");
	llvm::APFloat f(*r_semantics, 0);
	f.convertFromAPInt(
		arg->getAPValue(), true, APFloat::rmNearestTiesToEven);
	state.bindLocal(ki, ConstantExpr::alloc(f));
	break;
  }

  case Instruction::FCmp: {
	FCmpInst *fi = cast<FCmpInst>(i);
	ref<ConstantExpr> left(toConstant(state, eval(ki, 0, state), "fp"));
	ref<ConstantExpr> right(toConstant(state, eval(ki, 1, state), "fp"));
	if (	!fpWidthToSemantics(left->getWidth()) ||
		!fpWidthToSemantics(right->getWidth()))
	{
		TERMINATE_EXEC(this, state, "Unsupported FCmp operation");
		return;
	}

	DECL_APF(LHS, left);
	DECL_APF(RHS, right);
	APFloat::cmpResult CmpRes = LHS.compare(RHS);

	state.bindLocal(ki,
		MK_CONST(isFPPredicateMatched(CmpRes, fi->getPredicate()), 1));
	break;
	}

  case Instruction::InsertValue: instInsertValue(state, ki); break;

  case Instruction::ExtractValue: {
	KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(ki);
	ref<Expr>	agg, result;

	agg = eval(ki, 0, state);
	result = MK_EXTRACT(
		agg,
		kgepi->getOffsetBits()*8,
		kmodule->getWidthForLLVMType(i->getType()));

	state.bindLocal(ki, result);
	break;
  }


  // Vector instructions...
  case Instruction::ExtractElement: instExtractElement(state, ki); break;
  case Instruction::InsertElement:  instInsertElement(state, ki); break;
  case Instruction::ShuffleVector:  instShuffleVector(state, ki); break;

  default:
    if (isMallocLikeFn(i, NULL)) {
      instAlloc(state, ki);
      break;
    } else if (isFreeCall(i, NULL)) {
      executeFree(state, eval(ki, 0, state));
      break;
    }

    std::cerr << "OOPS! ";
    i->dump();
    TERMINATE_EXEC(this, state, "illegal instruction");
    break;
  }

	if (Expr::errors) {
		std::stringstream	ss;
		ss << "expr error\n";
		if (Expr::errorMsg.empty() == false)
			ss << "error msg: " << Expr::errorMsg << '\n';
		if (!Expr::errorExpr.isNull())
			ss << "bad expr: " << Expr::errorExpr << '\n';
		TERMINATE_ERROR(this, state, ss.str(), "expr.err");
		Expr::resetErrors();
	}
}

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
    TERMINATE_EARLY(this, *arr[i], "memory limit");
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
	if (DebugPrintInstructions &&
	    DebugPrintInstructions < state->totalInsts)
	{
		debugPrintInst(*state);
		if (DebugPrintValues && state->stack.hasLocal(ki)) {
			ref<Expr>	e(state->stack.readLocal(ki));
			if (!e.isNull())
				std::cerr << " = " << e << '\n';
		}
	}
	processTimers(state, MaxInstructionTime);
}

void Executor::handleMemoryUtilization(ExecutionState* &state)
{
	uint64_t mbs, instLimit;

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

	if (mbs <= 1.1*MaxMemory)
		return;

	instLimit = stats::instructions - lastMemoryLimitOperationInstructions;
	lastMemoryLimitOperationInstructions = stats::instructions;

	if (ReplayInhibitedForks && instLimit > 0x20000) {
		std::cerr << "[Exe] Replay inhibited forks.. COMPACTING!!\n";
		stateManager->compactPressureStates(MaxMemory);
		return;
	}

	/* resort to killing states if compacting  didn't help memory usage */
	killStates(state);

}

unsigned Executor::getNumStates(void) const { return stateManager->size(); }

unsigned Executor::getNumFullStates(void) const
{ return stateManager->getNonCompactStateCount(); }

bool Executor::runToFunction(ExecutionState* es, const KFunction* kf)
{
	bool		found_func = false;
	unsigned	test_c;

	test_c = interpreterHandler->getNumPathsExplored();
	do {
		if (es->getCurrentKFunc() == kf) {
			found_func = true;
			break;
		}
		stepStateInst(es);
	} while (
		test_c == interpreterHandler->getNumPathsExplored() &&
		!stateManager->isRemovedState(es));

	if (found_func == false)
		std::cerr << "[Exe] State exhausted\n";

	notifyCurrent(NULL);
	return found_func;
}

void Executor::setupInitFuncs(ExecutionState& initState)
{
	std::vector< ref<Expr> >	args;
	KFunction			*kf;

	kmodule->setupInitFuncs();
	if ((kf = kmodule->getInitFunc()) == NULL) return;

	executeCall(initState, NULL, kf->function, args);
	initState.stack.back().caller = NULL;
}

/* I don't remember why I wanted this feature */
static void doForceCOW(ExecutionState& es)
{
	std::cerr << "[Executor] Forcing COW: 'Sharing' exclusive objects.\n";
	for (auto &mop : es.addressSpace) {
		const ObjectState *os(es.addressSpace.findObject(mop.first));
		if (os->isZeroPage() || os->readOnly) continue;
		const_cast<ObjectState*>(os)->setOwner(~0 - 1);
	}
}

void Executor::run(ExecutionState &initState)
{
	PTree	*pt;

	currentState = &initState;
	haltExecution = false;

	if (mmu == NULL) mmu = MMU::create(*this);

	kmodule->bindModuleConstants(this);

	// Delay init till now so that ticks don't accrue during optimization
	initTimers();

	stateManager->setInitialState(&initState);
	pt = stateManager->getPTree();

	if (ForceCOW) doForceCOW(initState);

	initialStateCopy = initState.copy();
	assert (initState.ptreeNode != NULL);

	initialStateCopy->ptreeNode->markReplay();
	pt->splitStates(initState.ptreeNode, &initState, initialStateCopy);

	kmodule->setupFiniFuncs();
	setupInitFuncs(initState);

	PartSeedSetupDummy(this);

	if (replay != NULL) {
		std::cerr << "[Executor] Beginning Replay\n";
		replay->replay(this, &initState);
		std::cerr << "[Executor] Finished Replay\n";
	}

	if (Replay::isReplayOnly()) {
		std::cerr << "[Executor] Pure replay run complete.\n";
		stateManager->setupSearcher(
			UserSearcher::constructUserSearcher(*this));
		stateManager->teardownUserSearcher();
		goto eraseStates;
	}

	PartSeedSetup(this);

	/* I don't recall why I hold off on setting up the searcher
	 * until after replay. Hm. */
	stateManager->setupSearcher(UserSearcher::constructUserSearcher(*this));

	if (Replay::isReplayOnly())
		std::cerr << "[Executor] Pure replay run complete.\n";
	else
		runLoop();

	stateManager->teardownUserSearcher();

eraseStates:
	flushTimers();

	if (stateManager->empty())
		goto done;

	std::cerr << "KLEE: halting execution, dumping remaining states\n";
	haltExecution = true;

	for (auto sp : *stateManager) {
		ExecutionState &state = *sp;
		stepInstruction(state); // keep stats rolling
		if (DumpStatesOnHalt)
			TERMINATE_EARLY(this, state, "execution halting");
		else
			terminate(state);
	}
	notifyCurrent(0);

done:
	if (initialStateCopy) {
		ExecutionState	*root;
		root = (pt->isRoot(initialStateCopy)) ? initialStateCopy : NULL;
		if (root != NULL) {
			pt->removeRoot(root);
		} else {
			pt->remove(initialStateCopy->ptreeNode);
			delete initialStateCopy;
		}
		initialStateCopy = NULL;
	}

	currentState = NULL;
	delete mmu;
	mmu = NULL;
}

void Executor::step(void)
{
	currentState = stateManager->selectState(!onlyNonCompact);
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
}

void Executor::runLoop(void)
{ while (!stateManager->empty() && !haltExecution) step(); }

void Executor::notifyCurrent(ExecutionState* current)
{
	stateManager->commitQueue(current);
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
		// XXX: getRange() is really slow which makes print errors
		// really slow. Not worth it!
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
	TERMINATE_ERROR(this, state, "yielding state", "yield");
	// stateManager->yield(&state);
}

void Executor::terminate(ExecutionState &state)
{
	interpreterHandler->incPathsExplored();

	if (VerifyPath && !haltExecution) {
		static bool verifying = false;

		if (!verifying) {
			verifying = true;
			Replay::verifyPath(this, state);
			verifying = false;
		}
	}

	state.pc = state.prevPC;
	stateManager->queueRemove(&state);
}

void Executor::printStackTrace(const ExecutionState& st, std::ostream& os) const
{ st.dumpStack(os); }

void Executor::resolveExact(
	ExecutionState &state,
	ref<Expr> p,
	ExactResolutionList &results,
	const std::string &name)
{
	// XXX we may want to be capping this?
	ResolutionList rl;

	SymAddrSpace::resolve(state, solver, p, rl);

	ExecutionState *unbound = &state;
	for (auto &res : rl) {
		ref<Expr> inBounds;

		inBounds = MK_EQ(p, res.first->getBaseExpr());

		StatePair branches = fork(*unbound, inBounds, true);

		if (branches.first)
			results.push_back(std::make_pair(res, branches.first));

		unbound = branches.second;
		if (!unbound) // Fork failure
			break;
	}

	if (unbound) {
		TERMINATE_ERROR_LONG(this,
			*unbound,
			"memory error: invalid pointer: " + name,
			"ptr.err",
			getAddressInfo(*unbound, p), false);
	}
}

ObjectState* Executor::makeSymbolic(
	ExecutionState& state,
	const MemoryObject* mo,
	const char* arrPrefix)
{
	ObjectState	*os;
	ref<Array>	array;

	array = Array::create(state.getArrName(arrPrefix), mo->mallocKey);
	array = Array::uniqueByName(array);
	os = state.bindMemObjWriteable(mo, array.get());
	os->incCopyDepth();
	state.addSymbolic(const_cast<MemoryObject*>(mo) /* yuck */, array.get());

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
	for (auto &sym : state.getSymbolics()) {
		const MemoryObject				*mo;
		std::vector< ref<Expr> >::const_iterator	pi, pie;

		mo = sym.getMemoryObject();
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

	assert (st.checkCanary());

	for (auto &sym : st.getSymbolics()) objects.push_back(sym.getArray());

	a = Assignment(objects);

	/* second pass to bind early concretizations */
	for (auto &sym : st.getSymbolics()) {
		const std::vector<uint8_t>	*conc;
		if ((conc = sym.getConcretization()) != NULL)
			a.bindFree(sym.getArray(), *conc);
	}


	ok = solver->getInitialValues(st, a);
	if (ok) return true;

	klee_warning("can't compute initial values (invalid constraints?)!");

	bool mbt = false;
	ok = solver->mayBeTrue(st, st.constraints.getConjunction(), mbt);
	if (!ok) {
		std::cerr << "Couldn't compute satisfiability\n";
	} else if (!mbt) {
		std::cerr << "[Exe] INVALID constraints for getInitialValues\n";
	}

	if (DumpBadInitValues) {
		Query	q(st.constraints, MK_CONST(0, 1));
		q.print(std::cerr);
	}

	return false;
}

/* used for solution to state */
bool Executor::getSymbolicSolution(
	const ExecutionState &state,
	std::vector<
		std::pair<std::string,
			std::vector<unsigned char> > > &res)
{
	Assignment		a;

	if (PreferCex) {
		ExecutionState	tmp(state); /* XXX copy constructor broken */
		getSymbolicSolutionCex(state, tmp);
		if (!getSatAssignment(tmp, a)) return false;
	} else {
		if (!getSatAssignment(state, a))
			return false;
	}

	for (auto &sym : state.getSymbolics()) {
		const std::vector<unsigned char>	*v;

		/* ignore virtual symbolics used in lazy evaluation */
		if (sym.isVirtual()) continue;

		v = a.getBinding(sym.getArray());
		assert (v);
		res.push_back(std::make_pair(sym.getArray()->name, *v));
	}

	return true;
}

void Executor::doImpliedValueConcretization(
	ExecutionState &state,
	ref<Expr> e,
	ref<ConstantExpr> value)
{
	if (ivcEnabled == false)
		return;

	ImpliedValueList results;

	if (DebugCheckForImpliedValues)
		ImpliedValue::checkForImpliedValues(
			solver->getSolver(), e, value);

	ImpliedValue::getImpliedValues(e, value, results);

	/* ivc for when we don't have a read expr at top level */
	if (	e->getKind() == Expr::Eq &&
		e->getKid(1)->getWidth() != 1 &&
		e->getKid(1)->getKind() != Expr::Read &&
		e->getKid(0)->getKind() == Expr::Constant)
	{
		ref<ConstantExpr>	ce(cast<ConstantExpr>(e->getKid(0)));
		ImpliedValue::ivcStack(state.stack, e->getKid(1), ce);
	//	uint64_t		n0, n1;
	//	n0 = ImpliedValue::getMemUpdates();
	//	ImpliedValue::ivcMem(state.addressSpace, e->getKid(1), ce);
	//	n1 = ImpliedValue::getMemUpdates();
	//	std::cerr << "[IVC] Mem " << n1 - n0 << '\n';
	}

	for (auto &r : results) state.commitIVC(r.first, r.second);
}

void Executor::instGetElementPtr(ExecutionState& state, KInstruction *ki)
{
	KGEPInstruction		*kgepi;
	ref<Expr>		base;

	kgepi = static_cast<KGEPInstruction*>(ki);
	base = eval(ki, 0, state);

	for (auto &i : kgepi->indices) {
		uint64_t elementSize = i.second;
		ref<Expr> index = eval(ki, i.first, state);
		base = MK_ADD(
			base,
			MK_MUL(	Expr::createCoerceToPointerType(index),
				MK_PTR(elementSize)));
	}

	if (kgepi->getOffsetBits())
		base = MK_ADD(base, MK_PTR(kgepi->getOffsetBits()));

	state.bindLocal(ki, base);
}

void Executor::executeAllocConst(
	ExecutionState &state,
	uint64_t sz,
	bool isLocal,
	KInstruction *target,
	bool zeroMemory)
{
	ObjectPair	op;
	ObjectState	*os;

	op = state.allocate(sz, isLocal, false, state.prevPC->getInst());
	if (op_mo(op) == NULL) {
		state.bindLocal(target,	MK_PTR(0));
		return;
	}

	os = NULL;
	if (op_os(op)->isZeroPage() == false)
		os = state.addressSpace.getWriteable(op);

	if (os != NULL) {
		if (zeroMemory)
			os->initializeToZero();
		else
			os->initializeToRandom();
	}

	state.bindLocal(target, op_mo(op)->getBaseExpr());
}

void Executor::executeFree(
	ExecutionState &state,
	ref<Expr> address)
{
	const MemoryObject	*mo;
	const ConstantExpr	*ce;
	ObjectPair		op;

	if ((ce = dyn_cast<ConstantExpr>(address)) == NULL) {
		TERMINATE_EXEC(this, state, "non-const free address");
		return;
	}

	state.addressSpace.resolveOne(ce->getZExtValue(), op);

	mo = op_mo(op);
	if (mo == NULL) {
		TERMINATE_ERROR_LONG(this,
			state,
			"free invalid address",
			"free.err",
			getAddressInfo(state, address), false);
	} else if (mo->isLocal()) {
		TERMINATE_ERROR_LONG(this,
			state,
			"free of alloca",
			"free.err",
			getAddressInfo(state, address), false);
	} else if (mo->isGlobal()) {
		TERMINATE_ERROR_LONG(this,
			state,
			"free of global",
			"free.err",
			getAddressInfo(state, address), false);
	} else {
		state.unbindObject(mo);
	}
}


#include <malloc.h>
void Executor::handleMemoryPID(ExecutionState* &state)
{
	#define K_P	0.6
	#define K_D	0.1	/* damping factor-- damp changes in errors */
	#define K_I	0.0001  /* systematic error-- negative while ramping  */
	int		states_to_gen;
	int64_t		err;
	uint64_t	mbs;
	static int64_t	err_sum = -(int64_t)MaxMemory;
	static int64_t	last_err = 0;

	mbs = mallinfo().uordblks/(1024*1024);
	err = MaxMemory - mbs;

	states_to_gen = K_P*err + K_D*(err - last_err) + K_I*(err_sum);
	err_sum += err;
	last_err = err;

	if (states_to_gen < 0) {
		onlyNonCompact = false;
		stateManager->compactStates(-states_to_gen);
	}
}

ExeStateSet::const_iterator Executor::beginStates(void) const
{ return stateManager->begin(); }
ExeStateSet::const_iterator Executor::endStates(void) const
{ return stateManager->end(); }

void Executor::xferIterInit(
	struct XferStateIter& iter,
	ExecutionState* state,
	KInstruction* ki)
{
	iter.v = eval(ki, 0, *state);
	iter.ki = ki;
	iter.free = state;
	iter.getval_c = 0;
	iter.state_c = 0;
	iter.badjmp_c = 0;
}

#define MAX_BADJMP	10

bool Executor::xferIterNext(struct XferStateIter& iter)
{
	ExecutionState		*last_cur;
	Function		*iter_f;
	ref<ConstantExpr>	value;

	iter_f = NULL;
	last_cur = getCurrentState();
	while (iter.badjmp_c < MAX_BADJMP) {
		uint64_t	addr;
		unsigned	num_funcs = kmodule->getNumKFuncs();

		if (iter.free == NULL) return false;

		{
		WallTimer wt;
		if (solver->getValue(*(iter.free), iter.v, value) == false) {
			std::cerr << "===DYING ON NEXT secs=" <<  wt.checkSecs() << "\n";
			TERMINATE_EARLY(this,
				*(iter.free),
				"solver died on xferIterNext");
			return false;
		}
		}

		iter.getval_c++;
		iter.res = fork(*(iter.free), MK_EQ(iter.v, value), true);
		iter.free = iter.res.second;

		/* did not evaluate to 'true', getValue is busted? */
		if (iter.res.first == NULL) continue;

		addr = value->getZExtValue();
		iter.state_c++;

		/* currentstate hack for decode.err; gross, I know */
		currentState = iter.res.first;
		iter_f = getFuncByAddr(addr);
		currentState = last_cur;

		if (iter_f == NULL) {
			ExecutionState	*es(iter.res.first);

			if (iter.badjmp_c == 0) {
				klee_warning_once(
					(void*) (unsigned long) addr,
					"invalid function pointer: %p",
					(void*)addr);
			}

			if (	stateManager->isRemovedState(es) ||
				(stateManager->isAddedState(es) == false &&
				 stateManager->hasState(es) == false) ||
				es->getOnFini())
				continue;

			assert (es->checkCanary());


			std::cerr << "BAD POINTERS\n";
			TERMINATE_ERRORV(this,
				*es,
				"xfer iter error: bad pointer", "badjmp.err",
				"Bad Pointer: ", addr);
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
		if (iter.free == NULL) return false;


		std::cerr << "TOO MANY BAD POINTERS\n";

		TERMINATE_ERRORV(this,
			*(iter.free),
			"xfer iter error: too many bad jumps", "badjmp.err",
			"Symbolic address: ", iter.v);
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
	std::vector<ref<Expr>> conditions,
	bool isInternal,
	bool isBranch)
{ return forking->fork(current, conditions, isInternal, isBranch); }

bool Executor::hasState(const ExecutionState* es) const
{
	if (getCurrentState() == es)
		return true;

	for (auto cur_es : *stateManager)
		if (cur_es == es)
			return true;

	return false;
}

static StateSolver* createFastSolver(void)
{
	Solver		*s;
	TimedSolver	*ts;

	ts = createDummySolver();
	s = Solver::createChainWithTimedSolver("", "", ts);

	return new StateSolver(s, ts);
}

StateSolver* Executor::createSolverChain(
	double timeout,
	const std::string& qPath,
	const std::string& logPath)
{
	Solver		*s;
	TimedSolver	*timedSolver = NULL;
	StateSolver	*ts;

	if (timeout == 0.0)
		timeout = MaxSTPTime;

	s = Solver::createChainWithTimedSolver(qPath, logPath, timedSolver);
	ts = new StateSolver(s, timedSolver);
	ts->setTimeout(timeout);
	return ts;
}

void Executor::addModule(Module* m)
{
	assert (globals != NULL && "globals never declared?");
	kmodule->addModule(m);
	globals->updateModule();
}

bool Executor::startFini(ExecutionState& state)
{
	KFunction	*fini_kfunc = kmodule->getFiniFunc();

	if (fini_kfunc == NULL) return false;

	assert (state.getOnFini() == false);

	std::vector<ref<Expr> >	no_args;
	executeCallNonDecl(state, fini_kfunc->function, no_args);

	return true;
}

void Executor::terminateWith(Terminator& term, ExecutionState& state)
{
	if (term.terminate(state) == false) return;

	if (term.isInteresting(state) == false) {
		terminate(state);
		return;
	}

	if (state.getOnFini() == false && startFini(state)) {
		state.setFini(term);
		return;
	}

	term.process(state);
	terminate(state);
}

ExecutionState* Executor::pureFork(ExecutionState& es, bool compact)
{ return forking->pureFork(es, compact); }

void Executor::addFiniFunction(llvm::Function* f)
{ kmodule->addFiniFunction(kmodule->getKFunction(f)); }
void Executor::addInitFunction(llvm::Function* f)
{ kmodule->addInitFunction(kmodule->getKFunction(f)); }

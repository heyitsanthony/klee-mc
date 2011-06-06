#include "llvm/Target/TargetData.h"
#include "llvm/System/Path.h"
#include "klee/Config/config.h"
#include "../../lib/Core/StatsTracker.h"
#include "../../lib/Core/UserSearcher.h"
#include "../../lib/Core/PTree.h"
#include <vector>

#include "gueststate.h"
#include "guestcpustate.h"
#include "genllvm.h"
#include "vexhelpers.h"
#include "vexxlate.h"
#include "vexsb.h"
#include "static/Sugar.h"

#include "ExecutorVex.h"

using namespace klee;
using namespace llvm;

extern bool WriteTraces;

ExecutorVex::ExecutorVex(
	const InterpreterOptions &opts,
	InterpreterHandler *ih,
	GuestState	*in_gs)
: Executor(opts, ih),
  kmodule(0),
  gs(in_gs)
{
	/* XXX TODO: module flags */
	llvm::sys::Path LibraryDir(KLEE_DIR "/" RUNTIME_CONFIGURATION "/lib");
	Interpreter::ModuleOptions mod_opts(
		LibraryDir.c_str(),
		false,
		false,
		std::vector<std::string>());

	assert (gs);

	if (!theGenLLVM) theGenLLVM = new GenLLVM(in_gs);
	if (!theVexHelpers) theVexHelpers = new VexHelpers();

	xlate = new VexXlate();
	kmodule = new KModule(theGenLLVM->getModule());

	target_data = kmodule->targetData;
	dbgStopPointFn = kmodule->dbgStopPointFn; 

	// Initialize the context.
	assert(target_data->isLittleEndian() && "BIGENDIAN??");

	Context::initialize(
		target_data->isLittleEndian(),
		(Expr::Width) target_data->getPointerSizeInBits());

	kmodule->prepare(mod_opts, ih);

	if (StatsTracker::useStatistics()) {
		statsTracker = new StatsTracker(
			*this,
			kmodule,
			interpreterHandler->getOutputFilename("assembly.ll"),
			mod_opts.ExcludeCovFiles,
			userSearcherRequiresMD2U());
	}
}

ExecutorVex::~ExecutorVex(void)
{
	if (kmodule) delete kmodule;
	foreach (it, vsb_cache.begin(), vsb_cache.end()) {
		delete (*it).second;
	}
}

const Cell& ExecutorVex::eval(
	KInstruction *ki,
	unsigned index,
	ExecutionState &state) const
{
	int vnumber;

	assert(index < ki->inst->getNumOperands());
	
	vnumber = ki->operands[index];
	assert(	vnumber != -1 &&
		"Invalid operand to eval(), not a value or constant!");

	// Determine if this is a constant or not.
	if (vnumber < 0) return kmodule->constantTable[-vnumber - 2];

	return state.readLocalCell(state.stack.size() - 1, vnumber);
}

void ExecutorVex::runImage(void)
{
	ExecutionState	*state;
	Function	*init_func;

	// force deterministic initialization of memory objects
	srand(1);
	srandom(1);

	init_func = getFuncFromAddr((uint64_t)gs->getEntryPoint());
	state = new ExecutionState(kmodule->functionMap[init_func]);
	
//	if (UseEquivalentStateEliminator)
//		stateManager->setupESE(this, kmodule, state);

	prepArgs(state, init_func);
	
	processTree = new PTree(state);
	state->ptreeNode = processTree->root;

	fprintf(stderr, "COMMENCE THE RUN!!!!!!\n");
	run(*state);
	assert (0 == 1 && "PPPPPPPPP");
	delete processTree;
	processTree = 0;

	// hack to clear memory objects
	delete memory;
	memory = new MemoryManager();

	globalObjects.clear();
	globalAddresses.clear();

	if (statsTracker) statsTracker->done();
}


void ExecutorVex::prepArgs(ExecutionState* state, Function* f)
{
	setupRegisterContext(state, f);
	setupProcessMemory(state, f);
}

void ExecutorVex::setupProcessMemory(ExecutionState* state, Function* f)
{
	std::list<GuestMemoryRange*> memmap(gs->getMemoryMap());
	foreach (it, memmap.begin(), memmap.end()) {
		GuestMemoryRange	*gmr = *it;
		MemoryObject		*mmap_mo;
		ObjectState		*mmap_os;
		unsigned int		len;
		const char		*data;

		len = gmr->getBytes();
		mmap_mo = memory->allocateFixed(
			(uint64_t)gmr->getGuestAddr(),
			len,
			f->begin()->begin(),
			state);

		data = (const char*)gmr->getData();
		mmap_os = bindObjectInState(*state, mmap_mo, false);
		for (unsigned int i = 0; i < len; i++) {
			state->write8(mmap_os, i, data[i]);
		}
		delete gmr;
	}
}

void ExecutorVex::setupRegisterContext(ExecutionState* state, Function* f)
{
	std::vector<ref<Expr> > args;
	MemoryObject *state_regctx_mo= 0;
	unsigned int state_regctx_sz = gs->getCPUState()->getStateSize();

	state_regctx_mo = memory->allocate(
		state_regctx_sz,
		false, true,
		f->begin()->begin(), state);
	args.push_back(state_regctx_mo->getBaseExpr());

	if (symPathWriter) state->symPathOS = symPathWriter->open();
	if (statsTracker) statsTracker->framePushed(*state, 0);

	assert(args.size() == f->arg_size() && "wrong number of arguments");

	KFunction* kf = kmodule->functionMap[f];
	for (unsigned i = 0, e = f->arg_size(); i != e; ++i)
		bindArgument(kf, i, *state, args[i]);

	if (!state_regctx_mo) return;

	ObjectState *state_regctx_os;
	state_regctx_os = bindObjectInState(*state, state_regctx_mo, false);

	const char*  state_data = (const char*)gs->getCPUState()->getStateData();
	for (unsigned int i=0; i < state_regctx_sz; i++)  {
		state->write8(state_regctx_os, i, state_data[i]);
	}
}

void ExecutorVex::run(ExecutionState &initialState)
{
	bindModuleConstants();
	Executor::run(initialState);
}

void ExecutorVex::bindModuleConstants(void)
{
	foreach (it, kmodule->functions.begin(), kmodule->functions.end()) {
		KFunction *kf = *it;
		for (unsigned i=0; i<kf->numInstructions; ++i)
			bindInstructionConstants(kf->instructions[i]);
	}

	kmodule->constantTable = new Cell[kmodule->constants.size()];
	for (unsigned i = 0; i < kmodule->constants.size(); ++i) {
		Cell &c = kmodule->constantTable[i];
		c.value = evalConstant(kmodule->constants[i]);
	}
}

Function* ExecutorVex::getFuncFromAddr(uint64_t addr)
{
	return getEmitted(addr)->esb_f;
}

void ExecutorVex::executeInstruction(
	ExecutionState &state, KInstruction *ki)
{
	Executor::executeInstruction(state, ki);
}

////////////////

EmittedVexSB::~EmittedVexSB()
{
	delete esb_vsb;
}

EmittedVexSB* ExecutorVex::getEmitted(uint64_t guest_addr)
{
	uint64_t	host_addr;
	char		name_buf[64];
	VexSB		*vsb;
	Function	*f;
	EmittedVexSB	*esb;

	esb = vsb_cache[guest_addr];
	if (esb != NULL) return esb;

	host_addr = gs->addr2Host(guest_addr);

	/* XXX recongnize library ranges */
	if (!host_addr) host_addr = guest_addr;
	vsb = xlate->xlate((void*)host_addr, guest_addr);
	snprintf(name_buf, 64, "vsb_%p", guest_addr);
	f = vsb->emit(name_buf);
	f->dump();
	assert (f != NULL && "Could not emit VSB");

	esb = new EmittedVexSB(vsb, f);
	vsb_cache[guest_addr] = esb;

	kmodule->addFunction(f);

	return esb;
}

/* need to hand roll our own instRet because we want to be able to 
 * jump between super blocks based on ret values */
void ExecutorVex::instRet(ExecutionState &state, KInstruction *ki)
{
  ReturnInst *ri = cast<ReturnInst>(ki->inst);
  KInstIterator kcaller = state.stack.back().caller;
  Instruction *caller = kcaller ? kcaller->inst : 0;
  bool isVoidReturn = (ri->getNumOperands() == 0);
  ref<Expr> result = ConstantExpr::alloc(0, Expr::Bool);

  if (WriteTraces) {
    state.exeTraceMgr.addEvent(new FunctionReturnTraceEvent(state, ki));
  }
  
  if (!isVoidReturn) result = eval(ki, 0, state).value;
  
  if (state.stack.size() <= 1) {
    fprintf(stderr, "terminating initial stack frame\n");
    fprintf(stderr, "result: ");
    result->dump();
    assert(!caller && "caller set on initial stack frame");
    terminateStateOnExit(state);
    return;
  }
 
   fprintf(stderr, "OPPING FRAME ON RET\n");
  state.popFrame();

  if (statsTracker) statsTracker->framePopped(state);

  if (InvokeInst *ii = dyn_cast<InvokeInst>(caller)) {
    transferToBasicBlock(ii->getNormalDest(), caller->getParent(), state);
  } else {
    state.pc = kcaller;
    ++state.pc;
  }


  fprintf(stderr, "isVoidret\n");
  if (isVoidReturn) {
    // We check that the return value has no users instead of
    // checking the type, since C defaults to returning int for
    // undeclared functions.
    if (!caller->use_empty()) {
      terminateStateOnExecError(state, "return void when caller expected a result");
    }
    return;
  }

  assert (!isVoidReturn);
  const Type *t = caller->getType();
  if (t == Type::getVoidTy(getGlobalContext())) return;

  fprintf(stderr, "expr xlate\n");
  // may need to do coercion due to bitcasts
  Expr::Width from = result->getWidth();
  Expr::Width to = Expr::getWidthForLLVMType(t);
    
  if (from != to) {
    CallSite cs = (isa<InvokeInst>(caller) ? CallSite(cast<InvokeInst>(caller)) : 
                   CallSite(cast<CallInst>(caller)));

    // XXX need to check other param attrs ?
    if (cs.paramHasAttr(0, llvm::Attribute::SExt)) {
      result = SExtExpr::create(result, to);
    } else {
      result = ZExtExpr::create(result, to);
    }
  }
  fprintf(stderr, "bind local\n");
  bindLocal(kcaller, state, result);
}


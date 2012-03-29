#include "llvm/Target/TargetData.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Support/CommandLine.h"
#include "klee/Internal/Module/KModule.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_os_ostream.h"
#include "klee/Config/config.h"
#include "klee/breadcrumb.h"
#include "klee/Solver.h"
#include "../../lib/Solver/SMTPrinter.h"
#include "../../lib/Core/Globals.h"
#include "../../lib/Core/PrioritySearcher.h"
#include "../../lib/Core/SpecialFunctionHandler.h"
#include "../../lib/Core/TimingSolver.h"
#include "../../lib/Core/StatsTracker.h"
#include "../../lib/Core/ExeStateManager.h"
#include "../../lib/Core/UserSearcher.h"
#include "../../lib/Core/PTree.h"

#include <iomanip>
#include <unistd.h>
#include <stdio.h>
#include <vector>

#include "guest.h"
#include "guestcpustate.h"
#include "genllvm.h"
#include "vexhelpers.h"
#include "vexxlate.h"
#include "vexsb.h"
#include "vexfcache.h"
#include "static/Sugar.h"

#include "SyscallSFH.h"
#include "SysModel.h"

#include "ExecutorVex.h"
#include "ExeStateVex.h"

#include "RegPrioritizer.h"
#include "GuestPrioritizer.h"
#include "SyscallPrioritizer.h"

using namespace klee;
using namespace llvm;

extern bool WriteTraces;

extern bool SymArgs;
extern bool UsePrioritySearcher;

bool SymRegs;
bool UseConcreteVFS;

namespace
{
	cl::opt<bool,true> SymRegsProxy(
		"symregs",
		cl::desc("Mark initial register file as symbolic"),
		cl::location(SymRegs),
		cl::init(false));

	cl::opt<bool> UseCtrlGraph(
		"ctrl-graph",
		cl::desc("Compute control graph."),
		cl::init(false));

	cl::opt<bool> LogRegs(
		"logregs",
		cl::desc("Log registers."),
		cl::init(false));

	cl::opt<bool> OptimizeModule(
		"optimize",
		cl::desc("Optimize before execution"),
		cl::init(false));

	cl::opt<bool,true> ConcreteVfsProxy(
		"concrete-vfs",
		cl::desc("Treat absolute path opens as concrete"),
		cl::location(UseConcreteVFS),
		cl::init(false));

	cl::opt<bool> UseFDT(
		"use-fdt",
		cl::desc("Use TJ's FDT model"),
		cl::init(false));

	cl::opt<bool> DumpSyscallStates(
		"dump-syscall-state",
		cl::desc("Dump state constraints before a syscall"),
		cl::init(false));

	cl::opt<bool> CountLibraries(
		"count-lib-cov",
		cl::desc("Count library coverage"),
		cl::init(true));

	cl::opt<bool> AllowNegativeStack (
		"allow-negstack",
		cl::desc("Allow negative call stacks"),
		cl::init(false));

	cl::opt<bool> PrintNewRanges(
		"print-new-ranges",
		cl::desc("Print uncovered address ranges"),
		cl::init(false));

	cl::opt<bool> UseSyscallPriority(
		"use-syscall-pr",
		cl::desc("Use number of syscalls as priority"),
		cl::init(false));

	cl::opt<bool> UseRegPriority(
		"use-reg-pr",
		cl::desc("Use number of syscalls as priority"),
		cl::init(false));
}

ExecutorVex::ExecutorVex(InterpreterHandler *ih, Guest *in_gs)
: Executor(ih)
, gs(in_gs)
, native_code_bytes(0)
, ctrl_graph(in_gs)
{
	assert (kmodule == NULL && "KMod already initialized? My contract!");

	ExeStateBuilder::replaceBuilder(new ExeStateVexBuilder());

	if (gs->getMem()->is32Bit()) {
		MemoryManager::set32Bit();
	}

	if (UsePrioritySearcher) {
		Prioritizer	*pr;

		if (UseSyscallPriority)
			pr = new SyscallPrioritizer();
		else if (UseRegPriority)
			pr = new RegPrioritizer(*this);
		else
			pr = new GuestPrioritizer(*this);

		UserSearcher::setPrioritizer(pr);
	}

	/* XXX TODO: module flags */
	llvm::sys::Path LibraryDir(KLEE_DIR "/" RUNTIME_CONFIGURATION "/lib");
	Interpreter::ModuleOptions mod_opts(
		LibraryDir.c_str(),
		OptimizeModule,
		false,
		std::vector<std::string>());

	assert (gs);

	if (!theGenLLVM) theGenLLVM = new GenLLVM(in_gs);
	if (!theVexHelpers)
		theVexHelpers = VexHelpers::create(in_gs->getArch());

	std::cerr << "[klee-mc] Forcing fake vsyspage reads\n";
	theGenLLVM->setFakeSysReads();

	if (UseFDT)
		sys_model = new FDTModel(this);
	else
		sys_model = new LinuxModel(this);

	theVexHelpers->loadUserMod(sys_model->getModelFileName());

	xlate = new VexXlate(in_gs->getArch());
	xlate_cache = new VexFCache(xlate);

	assert (kmodule == NULL);
	kmodule = new KModule(theGenLLVM->getModule());

	target_data = kmodule->targetData;
	assert(target_data->isLittleEndian() && "BIGENDIAN??");
	Context::initialize(
		target_data->isLittleEndian(),
		(Expr::Width) target_data->getPointerSizeInBits());

	sfh = sys_model->allocSpecialFuncHandler(this);
	sfh->prepare();
	kmodule->prepare(mod_opts, ih);

	if (StatsTracker::useStatistics())
		statsTracker = new StatsTracker(
			*this,
			kmodule,
			interpreterHandler->getOutputFilename("assembly.ll"),
			mod_opts.ExcludeCovFiles,
			UserSearcher::userSearcherRequiresMD2U());

}

ExecutorVex::~ExecutorVex(void)
{
	delete xlate_cache;
	delete xlate;
	if (sfh) delete sfh;
	sfh = NULL;
	delete sys_model;
	if (kmodule) delete kmodule;
	kmodule = NULL;
}

ExecutionState* ExecutorVex::setupInitialStateEntry(uint64_t entry_addr)
{
	ExecutionState	*state;
	Function	*init_func;
	KFunction	*init_kfunc;
	bool		is_new;

	// force deterministic initialization of memory objects
	// XXX XXX XXX no determinism please
	// srand(1);
	// srandom(1);
	srand(time(0));
	srandom(time(0));

	// acrobatics because we have a fucking circular dependency
	// on the globaladdress stucture which keeps us from binding
	// the module constant table.

	/* add modules before initializing globals so that everything
	 * will link in properly */
	std::list<Module*> l = theVexHelpers->getModules();
	foreach (it, l.begin(), l.end())
		kmodule->addModule(*it);
	theVexHelpers->useExternalMod(kmodule->module);

	init_func = getFuncByAddrNoKMod(entry_addr, is_new);
	assert (init_func != NULL && "Could not get init_func. Bad decode?");
	if (init_func == NULL) {
		fprintf(stderr, "[klee-mc] COULD NOT GET INIT_FUNC\n");
		return NULL;
	}

	sys_model->installInitializers(init_func);

	init_kfunc = kmodule->addFunction(init_func);

	statsTracker->addKFunction(init_kfunc);
	bindKFuncConstants(init_kfunc);

	state = ExeStateBuilder::create(kmodule->getKFunction(init_func));

	prepState(state, init_func);
	globals = new Globals(kmodule, state, NULL);

	sys_model->installConfig(*state);

	sfh->bind();
	kf_scenter = kmodule->getKFunction("sc_enter");
	assert (kf_scenter && "Could not load sc_enter from runtime library");

	if (SymArgs) makeArgsSymbolic(state);

	pathTree = new PTree(state);
	state->ptreeNode = pathTree->root;


	return state;
}

ExecutionState* ExecutorVex::setupInitialState(void)
{
	return setupInitialStateEntry((uint64_t)gs->getEntryPoint());
}

void ExecutorVex::runImage(void)
{
	ExecutionState	*start_state;

	start_state = setupInitialState();
	if (start_state == NULL)
		return;

	run(*start_state);

	cleanupImage();
	fprintf(stderr, "OK.\n");
}

void ExecutorVex::cleanupImage(void)
{
	delete pathTree;
	pathTree = NULL;

	// hack to clear memory objects
	delete memory;
	memory = MemoryManager::create();

	if (statsTracker) statsTracker->done();
}


void ExecutorVex::makeArgsSymbolic(ExecutionState* state)
{
	std::vector<guest_ptr>	argv;

	argv = gs->getArgvPtrs();
	if (argv.size() == 0) return;

	fprintf(stderr,
		"[klee-mc] Making %d arguments symbolic\n",
		(int)(argv.size()-1));
	foreach (it, argv.begin()+1, argv.end()) {
		guest_ptr	p = *it;
		sfh->makeRangeSymbolic(
			*state,
			gs->getMem()->getHostPtr(p),
			gs->getMem()->strlen(p),
			"argv");
	}
}

void ExecutorVex::prepState(ExecutionState* state, Function* f)
{
	setupRegisterContext(state, f);
	setupProcessMemory(state, f);
}

// NOTE: PAGE_SIZE is defined un sys/user.h
void ExecutorVex::bindMappingPage(
	ExecutionState* state,
	Function* f,
	const GuestMem::Mapping& m,
	unsigned int pgnum)
{
	const char		*data;
	MemoryObject		*mmap_mo;
	ObjectState		*mmap_os;
	uint64_t		addr_base;
	uint64_t		heap_min, heap_max;

	assert (m.getBytes() > pgnum*PAGE_SIZE);
	assert ((m.getBytes() % PAGE_SIZE) == 0);
	assert ((m.offset.o & (PAGE_SIZE-1)) == 0);

	addr_base = ((uint64_t)gs->getMem()->getData(m))+(PAGE_SIZE*pgnum);

	mmap_os = state->allocateAt(addr_base, PAGE_SIZE, f->begin()->begin());
	mmap_mo = mmap_os->getObject();

	heap_min = ~0UL;
	heap_max = 0;

	if (m.type == GuestMem::Mapping::STACK) {
		mmap_mo->setName("stack");
	} else if (m.type == GuestMem::Mapping::HEAP) {
		mmap_mo->setName("heap");
		if (addr_base > heap_max) heap_max = addr_base;
		if (addr_base < heap_min) heap_min = addr_base;
	}else {
		mmap_mo->setName("guestimg");
	}

	data = (const char*)addr_base;
	for (unsigned int i = 0; i < PAGE_SIZE; i++) {
		/* bug fiend note:
		 * valgrind will complain on this line because of the
		 * data[i] on the syspage. Linux keeps a syscall page at
		 * 0xf..f600000 (vsyscall), but valgrind doesn't know this.
		 * This is safe, but will need a workaround *eventually* */
		state->write8(mmap_os, i, data[i]);
	}

	if (heap_min != ~0UL && heap_max != 0) {
		/* scanning memory is kind of stupid, but we're desperate */
		sys_model->setModelU64(
			kmodule->module, "heap_begin", heap_min);
		sys_model->setModelU64(
			kmodule->module, "heap_end",
			heap_max + 4096 /* max = start of last page */ );
	}


}

void ExecutorVex::bindMapping(
	ExecutionState* state,
	Function* f,
	GuestMem::Mapping m)
{
	unsigned int		len;

	len = m.getBytes();
	assert ((len % PAGE_SIZE) == 0);
	for (unsigned int i = 0; i < len/PAGE_SIZE; i++) {
		bindMappingPage(state, f, m, i);
	}
}

void ExecutorVex::setupProcessMemory(ExecutionState* state, Function* f)
{
	std::list<GuestMem::Mapping> memmap(gs->getMem()->getMaps());

	foreach (it, memmap.begin(), memmap.end()) {
		bindMapping(state, f, *it);
	}
}

MemoryObject* ExecutorVex::allocRegCtx(ExecutionState* state, Function* f)
{
	MemoryObject	*mo;
	unsigned int	state_regctx_sz;
	static unsigned id = 0;

	state_regctx_sz = gs->getCPUState()->getStateSize();

	if (f == NULL) f = state->getCurrentKFunc()->function;
	assert (f != NULL);

	mo = memory->allocate(
		state_regctx_sz,
		false, true,
		f->begin()->begin(),
		state);
	mo->setName("regctx"+llvm::utostr(++id));
	assert (mo != NULL);

	return mo;
}

void ExecutorVex::setupRegisterContext(ExecutionState* state, Function* f)
{
	MemoryObject			*state_regctx_mo;
	ObjectState 			*state_regctx_os;
	KFunction			*kf;
	unsigned int			state_regctx_sz;

	state_regctx_mo = allocRegCtx(state, f);
	es2esv(*state).setRegCtx(state_regctx_mo);

	if (SymRegs) executeMakeSymbolic(*state, state_regctx_mo, "reg");

	state_regctx_sz = gs->getCPUState()->getStateSize();

	if (symPathWriter) state->symPathOS = symPathWriter->open();
	if (statsTracker) statsTracker->framePushed(*state, 0);

	kf = kmodule->getKFunction(f);

	assert (f->arg_size() == 1);
	state->bindArgument(kf, 0, state_regctx_mo->getBaseExpr());

	if (SymRegs) return;

	state_regctx_os = state->bindMemObj(state_regctx_mo);

	const char*  state_data = (const char*)gs->getCPUState()->getStateData();
	for (unsigned int i=0; i < state_regctx_sz; i++)
		state->write8(state_regctx_os, i, state_data[i]);

}

void ExecutorVex::run(ExecutionState &initialState)
{
	bindModuleConstants();
	Executor::run(initialState);
}

Function* ExecutorVex::getFuncByAddrNoKMod(uint64_t guest_addr, bool& is_new)
{
	void		*host_addr;
	Function	*f;
	VexSB		*vsb;

	if (	guest_addr == 0 ||
		((guest_addr > 0x7fffffffffffULL) &&
		((guest_addr & 0xfffffffffffff000) != 0xffffffffff600000)) ||
		guest_addr == 0xffffffff)
	{
		/* short circuit obviously bad addresses */
		return NULL;
	}

	/* XXX: This is wrong because it doesn't acknowledge write-backs */
	/* The right way to do it would involve grabbing from the state's MO */
	host_addr = gs->getMem()->getHostPtr(guest_ptr(guest_addr));

	/* cached => already seen it */
	f = xlate_cache->getCachedFunc(guest_ptr(guest_addr));
	if (f != NULL) {
		is_new = false;
		return f;
	}

	/* Need to load the function. First, make sure that addr is mapped. */
	/* XXX: this is broken for code that is allocated *after* guest is
	 * loaded and snapshot */
	GuestMem::Mapping	m;
	if (gs->getMem()->lookupMapping(guest_ptr(guest_addr), m) == false) {
		return NULL;
	}

	/* !cached => put in cache, alert kmodule, other bookkepping */
	f = xlate_cache->getFunc(host_addr, guest_ptr(guest_addr));
	if (f == NULL) return NULL;

	/* need to know func -> vsb to compute func's guest address */
	vsb = xlate_cache->getCachedVSB(guest_ptr(guest_addr));
	assert (vsb && "Dropped VSB too early?");
	func2vsb_table[(uint64_t)f] = vsb;

	is_new = true;
	native_code_bytes += vsb->getEndAddr() - vsb->getGuestAddr();

	if (PrintNewRanges) {
		std::cerr << "[UNCOV] "
			<< (void*)vsb->getGuestAddr().o
			<< "-"
			<< (void*)vsb->getEndAddr().o << " : "
			<< gs->getName(vsb->getGuestAddr())
			<< '\n';
	}

	return f;
}

const VexSB* ExecutorVex::getFuncVSB(Function* f) const
{
	func2vsb_map::const_iterator	it;

	it = func2vsb_table.find((uint64_t)f);
	if (it == func2vsb_table.end())
		return NULL;

	return it->second;
}

#define LIBRARY_BASE_GUESTADDR	((uint64_t)0x10000000)

Function* ExecutorVex::getFuncByAddr(uint64_t guest_addr)
{
	KFunction	*kf;
	Function	*f;
	bool		is_new;

	//let functions inside our bitcode syscall model be called.
	//there is surely some shitty overlap problem now
	if (legalFunctions.count(guest_addr))
		return (Function*)guest_addr;

	f = getFuncByAddrNoKMod(guest_addr, is_new);
	if (f == NULL) return NULL;
	if (!is_new) return f;

	/* insert it into the kmodule */
	if (UseCtrlGraph) {
		ctrl_graph.addFunction(f, guest_ptr(guest_addr));
		std::ostream* of;
		of = interpreterHandler->openOutputFile("statics.dot");
		if (of) {
			ctrl_graph.dumpStatic(*of);
			delete of;
		}
	}

	if (CountLibraries == false) {
		/* is library address? */
		if (guest_addr > LIBRARY_BASE_GUESTADDR) {
			kf = kmodule->addUntrackedFunction(f);
		} else {
			kf = kmodule->addFunction(f);
		}
	} else
		kf = kmodule->addFunction(f);

	statsTracker->addKFunction(kf);
	bindKFuncConstants(kf);
	kmodule->bindModuleConstTable(this);

	return f;
}

void ExecutorVex::executeInstruction(
        ExecutionState &state,
        KInstruction *ki)
{
        Executor::executeInstruction(state, ki);
}

/* need to hand roll our own instRet because we want to be able to
 * jump between super blocks based on ret values */
void ExecutorVex::instRet(ExecutionState &state, KInstruction *ki)
{
	Function		*cur_func;
	VexSB			*vsb;

	/* need to trapeze between VSB's; depending on exit type,
	 * translate VSB exits into LLVM instructions on the fly */
	cur_func = (state.stack.back()).kf->function;


	vsb = func2vsb_table[(uintptr_t)cur_func];
	if (vsb == NULL && cur_func != kf_scenter->function) {
		/* no VSB => outcall to externa LLVM bitcode;
		 * use default KLEE return handling */
		assert (state.stack.size() > 1);
		Executor::retFromNested(state, ki);
		return;
	}

	if (cur_func == kf_scenter->function) {
		/* If leaving the sc_enter function, we need to
		 * know to pop the stack. Otherwies, it might
		 * look like a jump and keep stale entries on board */
		markExit(state, GE_RETURN);
	}

	handleXfer(state, ki);
}

void ExecutorVex::markExit(ExecutionState& state, uint8_t v)
{
	ObjectState		*state_regctx_os;

	gs->getCPUState()->setExitType(GE_IGNORE);
	state_regctx_os = GETREGOBJ(state);
	state.write8(
		state_regctx_os,
		gs->getCPUState()->getExitTypeOffset(),
		v);
}

void ExecutorVex::logXferRegisters(ExecutionState& state)
{
	const ObjectState*	state_regctx_os;
	unsigned int		reg_sz;
	uint8_t			*crumb_buf, *crumb_base;
	struct breadcrumb	*bc;

	updateGuestRegs(state);
	if (!LogRegs) return;

	/* XXX: expensive-- lots of storage */
	reg_sz = gs->getCPUState()->getStateSize();
	crumb_base = new uint8_t[sizeof(struct breadcrumb)+(reg_sz*2)];
	crumb_buf = crumb_base;
	bc = reinterpret_cast<struct breadcrumb*>(crumb_base);

	bc_mkhdr(bc, BC_TYPE_VEXREG, 0, reg_sz*2);
	crumb_buf += sizeof(struct breadcrumb);

	/* 1. store concrete cache */
	memcpy(	crumb_buf,
		gs->getCPUState()->getStateData(),
		reg_sz);
	crumb_buf += reg_sz;

	/* 2. store concrete mask */
	state_regctx_os = GETREGOBJRO(state);
	for (unsigned int i = 0; i < reg_sz; i++) {
		crumb_buf[i] =(state_regctx_os->isByteConcrete(i))
			? 0xff
			: 0;
	}

	es2esv(state).recordBreadcrumb(bc);

	delete [] crumb_base;
}

/* handle transfering between VSB's */
void ExecutorVex::handleXfer(ExecutionState& state, KInstruction *ki)
{
	GuestExitType		exit_type;

	logXferRegisters(state);

	exit_type = gs->getCPUState()->getExitType();
	markExit(state, GE_IGNORE);

	switch(exit_type) {
	case GE_CALL:
		handleXferCall(state, ki);
		return;
	case GE_RETURN:
		handleXferReturn(state, ki);
		return;
	case GE_SYSCALL:
		handleXferSyscall(state, ki);
		return;
	case GE_EMWARN:
		std::cerr << "[VEXLLVM] VEX Emulation warning!?" << std::endl;
	case GE_IGNORE:
		handleXferJmp(state, ki);
		return;
	case GE_SIGSEGV:
		std::cerr << "[VEXLLVM] Caught SigSegV. Error Exit.\n";
		terminateStateOnError(
			state,
			"VEX SIGSEGV error: jump to sigsegv",
			"sigsegv.err");
		return;

	case GE_SIGTRAP:
		std::cerr << "[VEXLLVM] Caught SigTrap. Exiting\n";
		terminateStateOnExit(state);
		return;
	default:
		fprintf(stderr, "WTF: EXIT_TYPE=%d\n", exit_type);
		assert (0 == 1 && "SPECIAL EXIT TYPE");
	}

	/* XXX need better bad stack frame handling */
	ref<Expr> result = ConstantExpr::alloc(0, Expr::Bool);
	result = eval(ki, 0, state).value;

	fprintf(stderr, "terminating initial stack frame\n");
	fprintf(stderr, "result: ");
	result->dump();
	terminateStateOnExit(state);
}


void ExecutorVex::handleXferJmp(ExecutionState& state, KInstruction* ki)
{
	struct XferStateIter	iter;
	xferIterInit(iter, &state, ki);
	while (xferIterNext(iter)) {
		jumpToKFunc(*(iter.res.first), kmodule->getKFunction(iter.f));
	}
}

void ExecutorVex::jumpToKFunc(ExecutionState& state, KFunction* kf)
{
	const MemoryObject	*regctx_mo;

	/* save, pop off old state */
	regctx_mo = es2esv(state).getRegCtx();
	state.xferFrame(kf);

	/* set new state */
	state.pc = kf->instructions;
	state.bindArgument(kf, 0, regctx_mo->getBaseExpr());
}

/* xfers are done with an address in the return value of the next place to
 * jump.  f(x) returns g => g(x) -> f(x). (g directly follows f) */
void ExecutorVex::handleXferCall(ExecutionState& state, KInstruction* ki)
{
	std::vector< ref<Expr> > 	args;
	struct XferStateIter		iter;

	args.push_back(es2esv(state).getRegCtx()->getBaseExpr());
	xferIterInit(iter, &state, ki);
	while (xferIterNext(iter)) {
		executeCall(*(iter.res.first), ki, iter.f, args);
	}
}

/**
 * Pass return address and register context to sc handler.
 * SC handler has additional special funcs for manipulating (see SFH)
 * the register context.
 */
void ExecutorVex::handleXferSyscall(
	ExecutionState& state, KInstruction* ki)
{
	std::vector< ref<Expr> > 	args;
	struct XferStateIter		iter;
	uint64_t			sysnr;

	if (DumpSyscallStates) {
		static int	n = 0;
		char		prefix[32];
		Query		q(
			state.constraints,
			ConstantExpr::create(1, 1));

		n++;
		sprintf(prefix, "sc-%d.%d",
			es2esv(state).getSyscallCount(),
			n);
		SMTPrinter::dump(q, prefix);
	}

	es2esv(state).incSyscallCount();

	sysnr = 0;
	switch (gs->getArch()) {
	case Arch::X86_64:
		state.addressSpace.copyToBuf(
			es2esv(state).getRegCtx(), &sysnr, 0, 8);
		break;
	case Arch::ARM:
		state.addressSpace.copyToBuf(
			es2esv(state).getRegCtx(), &sysnr, 4*7, 4);
		break;
	case Arch::I386:
		state.addressSpace.copyToBuf(
			es2esv(state).getRegCtx(), &sysnr, 0, 4);
		break;
	default:
		assert (0 == 1 && "ULP");
	}


	fprintf(stderr, "before syscall %d(?): states=%d. objs=%d. st=%p. n=%d\n",
		(int)sysnr,
		stateManager->size(),
		state.getNumSymbolics(),
		(void*)&state,
		es2esv(state).getSyscallCount());

	/* arg0 = regctx, arg1 = jmpptr */
	args.push_back(es2esv(state).getRegCtx()->getBaseExpr());
	args.push_back(eval(ki, 0, state).value);

	executeCall(state, ki, kf_scenter->function, args);
}

void ExecutorVex::handleXferReturn(
	ExecutionState& state, KInstruction* ki)
{
	struct XferStateIter	iter;
	unsigned		stack_depth;

	stack_depth = state.stack.size();
	if (!AllowNegativeStack && stack_depth == 1) {
		/* Call-stack is exhausted. KLEE resumes
		 * control. */
		terminateStateOnExit(state);
		return;
	}

	assert (stack_depth >= 1);

	xferIterInit(iter, &state, ki);
	while (xferIterNext(iter)) {
		ExecutionState*	new_state;

		new_state = iter.res.first;
		if (stack_depth > 1) {
			/* pop frame to represent a 'return' */
			/* if the depth < 1, we treat a ret like a
			 * jump because what can you do */
			new_state->popFrame();
		}

		jumpToKFunc(*new_state, kmodule->getKFunction(iter.f));
	}
}

/* this is when the llvm code has a
 * 'call' which resolves to code we can't link in directly.  */
void ExecutorVex::callExternalFunction(
	ExecutionState &state,
	KInstruction *target,
	llvm::Function *function,
	std::vector< ref<Expr> > &arguments)
{
	// check if specialFunctionHandler wants it
	if (sfh->handle(state, function, target, arguments))
		return;

	std::cerr
		<< "KLEE:ERROR: Calling non-special external function : "
		<< function->getNameStr() << "\n";
	terminateStateOnError(state, "externals disallowed", "user.err");
}


/* copy concrete parts into guest regs. */
void ExecutorVex::updateGuestRegs(ExecutionState& state)
{
	void		*guest_regs;
	guest_regs = gs->getCPUState()->getStateData();
	state.addressSpace.copyToBuf(es2esv(state).getRegCtx(), guest_regs);
}

void ExecutorVex::printStackTrace(ExecutionState& st, std::ostream& os) const
{
	unsigned idx = 0;
	foreach (it, st.stack.rbegin(), st.stack.rend())
	{
		StackFrame	&sf = *it;
		Function	*f = sf.kf->function;
		VexSB		*vsb;
		func2vsb_map::const_iterator	f2v_it;

		f2v_it = func2vsb_table.find((uint64_t)f);
		vsb = NULL;
		if (f2v_it != func2vsb_table.end())
			vsb = f2v_it->second;

		os << "\t#" << idx++ << " in " << f->getNameStr();
		if (vsb) {
			os << " (" << gs->getName(vsb->getGuestAddr()) << ")";
		}
		os << "\n";
	}
}

std::string ExecutorVex::getPrettyName(llvm::Function* f) const
{
	VexSB				*vsb;
	func2vsb_map::const_iterator	f2v_it;

	f2v_it = func2vsb_table.find((uint64_t)f);
	vsb = NULL;
	if (f2v_it != func2vsb_table.end())
		vsb = f2v_it->second;

	if (vsb != NULL)
		return gs->getName(vsb->getGuestAddr());

	return Executor::getPrettyName(f);
}

void ExecutorVex::printStateErrorMessage(
	ExecutionState& state,
	const std::string& message,
	std::ostream& os)
{
	Function*	top_f;

	/* TODO: get line information for state.prevPC */
	klee_message("ERROR: %s", message.c_str());

	os << "Error: " << message << "\n";

	os << "Objects: " << std::endl;
	state.addressSpace.printObjects(os);

	os << "\nRegisters: \n";
	gs->getCPUState()->print(os);

	os << "\nStack: \n";
	printStackTrace(state, os);

	if (state.prevPC && state.prevPC->getInst()) {
		raw_os_ostream	ros(os);
		ros << "problem PC:\n";
		ros << *(state.prevPC->getInst());
		ros << "\n";
	}

	top_f = state.stack.back().kf->function;
	os << "Func: ";
	if (top_f) {
		raw_os_ostream ros(os);
		ros << top_f;
	} else
		os << "???";
	os << "\n";

	os << "Constraints: \n";
	state.constraints.print(os);
}

ref<Expr> ExecutorVex::getCallArg(ExecutionState& state, unsigned int n) const
{
	const ObjectState	*regobj;

	regobj = GETREGOBJRO(state);
	return state.read(
		regobj,
		gs->getCPUState()->getFuncArgOff(n),
		gs->getMem()->is32Bit() ? 32 : 64);
}

ref<Expr> ExecutorVex::getRetArg(ExecutionState& state) const
{
	const ObjectState	*regobj;

	regobj = GETREGOBJRO(state);
	return state.read(
		regobj,
		gs->getCPUState()->getRetOff(),
		gs->getMem()->is32Bit() ? 32 : 64);
}

uint64_t ExecutorVex::getStateStack(ExecutionState& es) const
{
	ref<Expr>		stack_e;
	const ConstantExpr	*stack_ce;
	const ObjectState	*os;

	/* XXX: not 32-bit clean */
	os = GETREGOBJRO(es);
	stack_e = es.read(
		os,
		getGuest()->getCPUState()->getStackRegOff(),
		gs->getMem()->is32Bit() ? 32 : 64);
	stack_ce = dyn_cast<ConstantExpr>(stack_e);
	if (stack_ce == NULL) {
		std::cerr << "warning: COULD NOT WATCH BY STACK!!!!!!\n";
		std::cerr << "symbolic stack pointer: ";
		stack_e->print(std::cerr);
		std::cerr << '\n';
		return 0;
	}

	return stack_ce->getZExtValue();
}

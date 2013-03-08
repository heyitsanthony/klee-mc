#include <llvm/DataLayout.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_os_ostream.h>
#include "klee/Config/config.h"
#include "klee/breadcrumb.h"

#include "klee/Solver.h"
#include "../../lib/Solver/SMTPrinter.h"
#include "../../lib/Core/Globals.h"
#include "../../lib/Searcher/PrioritySearcher.h"
#include "../../lib/Searcher/UserSearcher.h"
#include "../../lib/Core/StatsTracker.h"
#include "../../lib/Core/ExeStateManager.h"
#include "../../lib/Core/MemoryManager.h"

#include <stdio.h>
#include <vector>

#include "KModuleVex.h"
#include "symbols.h"
#include "guestcpustate.h"
#include "genllvm.h"
#include "vexhelpers.h"
#include "vexsb.h"
#include "static/Sugar.h"

#include "SyscallSFH.h"
#include "SysModel.h"

#include "ExecutorVex.h"
#include "ExeStateVex.h"

#include "RegPrioritizer.h"
#include "SyscallPrioritizer.h"
#include "KleeHandlerVex.h"

extern "C"
{
#include "valgrind/libvex_guest_amd64.h"
#include "valgrind/libvex_guest_x86.h"
#include "valgrind/libvex_guest_arm.h"
}

using namespace klee;
using namespace llvm;

extern bool WriteTraces;

extern bool SymArgs;
extern bool UsePrioritySearcher;

bool SymRegs = false;
bool UseConcreteVFS;

#define GET_SFH(x)	static_cast<SyscallSFH*>(x)

namespace
{
	cl::opt<bool> ShowSyscalls("show-syscalls");
	cl::opt<bool> SymMagic(
		"sym-magic", cl::desc("Mark 'magic' 0xa3 bytes as symbolic"));

	cl::opt<bool> KeepDeadStack(
		"keep-dead-stack",
		cl::desc("Keep registers in dead stack frames"),
		cl::init(false));

	cl::opt<bool,true> SymRegsProxy(
		"symregs",
		cl::desc("Mark initial register file as symbolic"),
		cl::location(SymRegs));

	cl::opt<bool> LogRegs("logregs", cl::desc("Log registers."));

	cl::opt<bool,true> ConcreteVfsProxy(
		"concrete-vfs",
		cl::desc("Treat absolute path opens as concrete"),
		cl::location(UseConcreteVFS));

	cl::opt<bool> UseFDT("use-fdt", cl::desc("Use TJ's FDT model"));
	cl::opt<bool> UseNT("use-nt", cl::desc("Use NT model"));
	cl::opt<bool> UseSysNone("use-sysnone", cl::desc("No System Calls"));

	cl::opt<bool> DumpSyscallStates(
		"dump-syscall-state",
		cl::desc("Dump state constraints before a syscall"));

	cl::opt<bool> AllowNegativeStack (
		"allow-negstack",
		cl::desc("Allow negative call stacks"),
		cl::init(true));

	cl::opt<bool> UseSyscallPriority(
		"use-syscall-pr",
		cl::desc("Use number of syscalls as priority"));

	cl::opt<bool> UseRegPriority(
		"use-reg-pr", cl::desc("Use number of syscalls as priority"));

	cl::opt<std::string> RunSym(
		"run-func", cl::desc("Name of function to run"));
}

ExecutorVex::ExecutorVex(InterpreterHandler *ih)
: Executor(ih)
, gs(dynamic_cast<KleeHandlerVex*>(ih)->getGuest())
, img_init_func(0)
, img_init_func_addr(0)
{
	assert (kmodule == NULL && "KMod already initialized? My contract!");

	ExeStateBuilder::replaceBuilder(new ExeStateVexBuilder());
	ExeStateVex::setBaseGuest(gs);
	ExeStateVex::setBaseStack(gs->getCPUState()->getStackPtr().o);

	if (gs->getMem()->is32Bit()) {
		MemoryManager::set32Bit();
	}

	if (UsePrioritySearcher) {
		Prioritizer	*pr = NULL;

		if (UseSyscallPriority)
			pr = new SyscallPrioritizer();
		else if (UseRegPriority)
			pr = new RegPrioritizer(*this);

		UserSearcher::setPrioritizer(pr);
	}

	/* XXX TODO: module flags */
	llvm::sys::Path LibraryDir(KLEE_DIR "/" RUNTIME_CONFIGURATION "/lib");
	Interpreter::ModuleOptions mod_opts(
		LibraryDir.c_str(),
		false, // XXX: DUMMY. REMOVE ME; OptimizeModule,
		false,
		std::vector<std::string>());

	assert (gs);

	if (!theGenLLVM) theGenLLVM = new GenLLVM(gs);
	if (!theVexHelpers)
		theVexHelpers = VexHelpers::create(gs->getArch());

	std::cerr << "[klee-mc] Forcing fake vsyspage reads\n";
	theGenLLVM->setFakeSysReads();

	if (UseFDT)	sys_model = new FDTModel(this);
	else if (UseNT) sys_model = new W32Model(this);
	else if (UseSysNone) sys_model = new NoneModel(this);
	else		sys_model = new LinuxModel(this);

	theVexHelpers->loadUserMod(sys_model->getModelFileName());

	/* occasionally we want to load something elsewhere
	 * (ARM 0x8000 code base on fogger,
	 *  0xf..fe000 faketimer page on fogger,
	 *  ...) --
	 * this creates problems because normally the codegen will
	 * update loads/stores to reflect these new addresses.
	 * unsetting the bias should revert to unbiased behavior */
	if (getenv("VEXLLVM_BASE_BIAS") != NULL)
		unsetenv("VEXLLVM_BASE_BIAS");

	assert (kmodule == NULL);

	km_vex = new KModuleVex(this, mod_opts, gs);
	kmodule = km_vex;

	data_layout = kmodule->dataLayout;
	assert(data_layout->isLittleEndian() && "BIGENDIAN??");
	Context::initialize(
		data_layout->isLittleEndian(),
		(Expr::Width) data_layout->getPointerSizeInBits());

	sfh = sys_model->allocSpecialFuncHandler(this);
	sfh->prepare();
	kmodule->prepare(ih);

	if (StatsTracker::useStatistics())
		statsTracker = new StatsTracker(
			*this,
			kmodule,
			interpreterHandler->getOutputFilename("assembly.ll"),
			mod_opts.ExcludeCovFiles);
}

ExecutorVex::~ExecutorVex(void)
{
	delete sys_model;
	if (kmodule) delete kmodule;
	kmodule = NULL;
}

llvm::Function* ExecutorVex::setupRuntimeFunctions(uint64_t entry_addr)
{
	KFunction	*init_kfunc;
	bool		is_new;

	if (img_init_func != NULL) {
		assert (entry_addr == img_init_func_addr);
		return img_init_func;
	}

	assert (entry_addr != 0);

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

	img_init_func = km_vex->getFuncByAddrNoKMod(entry_addr, is_new);
	if (img_init_func == NULL) {
		std::cerr << "[klee-mc] COULD NOT GET INIT_FUNC\n";
		return NULL;
	}

	sys_model->installInitializers(img_init_func);
	init_kfunc = kmodule->addFunction(img_init_func);

	statsTracker->addKFunction(init_kfunc);
	km_vex->bindKFuncConstants(this, init_kfunc);

	img_init_func_addr = entry_addr;
	return img_init_func;
}


ExecutionState* ExecutorVex::setupInitialStateEntry(uint64_t entry_addr)
{
	llvm::Function	*init_func;
	ExecutionState	*state;

	init_func = setupRuntimeFunctions(entry_addr);
	assert (init_func != NULL && "Could not get init_func. Bad decode?");

	state = ExeStateBuilder::create(kmodule->getKFunction(init_func));
	assert (state != NULL);

	prepState(state, init_func);
	globals = new Globals(kmodule, state, NULL);

	sys_model->installConfig(*state);

	sfh->bind();
	kf_scenter = kmodule->getKFunction("sc_enter");
	assert (kf_scenter && "Could not load sc_enter from runtime library");

	if (SymArgs) makeArgsSymbolic(state);
	if (SymMagic) makeMagicSymbolic(state);

	return state;
}

ExecutionState* ExecutorVex::setupInitialState(void)
{ return setupInitialStateEntry((uint64_t)gs->getEntryPoint()); }

void ExecutorVex::runSym(const char* xchk_fn)
{
	ExecutionState	*start_state;
	const Symbol	*sym;
	uint64_t	base_addr;

	if (!RunSym.empty()) xchk_fn = RunSym.c_str();

	if (xchk_fn != NULL) {
		const Symbols	*syms;

		syms = gs->getSymbols();
		sym = syms->findSym(xchk_fn);
		fprintf(stderr, "[EXEVEX] Using symbol: %s\n", xchk_fn);
		assert (sym != NULL && "Couldn't find sym");
	} else
		sym = NULL;

	ExecutionState::setMemoryManager(memory);
	base_addr = (sym)
		? sym->getBaseAddr()
		: ((uint64_t)gs->getEntryPoint());
	start_state = setupInitialStateEntry(base_addr);
	if (start_state == NULL)
		return;

	run(*start_state);
	cleanupImage();
}

void ExecutorVex::cleanupImage(void)
{
	delete memory;
	memory = MemoryManager::create();

	if (statsTracker) statsTracker->done();
}

void ExecutorVex::makeMagicSymbolic(ExecutionState* state)
{
	std::vector<std::pair<void*, unsigned> >	exts;

	exts = state->addressSpace.getMagicExtents();
	if (exts.size() == 0) return;

	std::cerr << "[klee-mc] Set " << exts.size() << " extents symbolic\n";

	foreach (it, exts.begin(), exts.end())
		GET_SFH(sfh)->makeRangeSymbolic(
			*state, it->first, it->second, "magic");
}

void ExecutorVex::makeArgsSymbolic(ExecutionState* state)
{
	std::vector<guest_ptr>	argv;

	argv = gs->getArgvPtrs();
	if (argv.size() == 0) return;

	std::cerr << "[klee-mc] Set " << (argv.size()-1) << " args symbolic\n";

	foreach (it, argv.begin()+1, argv.end()) {
		guest_ptr	p = *it;
		GET_SFH(sfh)->makeRangeSymbolic(
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
	const ObjectState	*mmap_os_c;
	ObjectState		*mmap_os;
	uint64_t		addr_base;
	uint64_t		heap_min, heap_max;

	assert (m.getBytes() > pgnum*PAGE_SIZE);
	assert ((m.getBytes() % PAGE_SIZE) == 0);
	assert ((m.offset.o & (PAGE_SIZE-1)) == 0);

	addr_base = ((uint64_t)gs->getMem()->getData(m))+(PAGE_SIZE*pgnum);

	mmap_os_c = state->allocateAt(addr_base, PAGE_SIZE, f->begin()->begin());
	mmap_mo = const_cast<MemoryObject*>(
		state->addressSpace.resolveOneMO(addr_base));

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

	/* optimize for zero pages */
	data = (const char*)addr_base;
	unsigned i = 0;

#ifdef CLUSTER_HACKS
	if (data == (void*)0xffffffffff5fe000)
		return;
#endif

	for (i = 0; i < PAGE_SIZE; i++) {
		/* can keep zero page? */
		if (!data[i]) continue;
		/* can't keep zero page */
		mmap_os = state->addressSpace.getWriteable(mmap_mo, mmap_os_c);
		break;
	}

	for (; i < PAGE_SIZE; i++) {
		/* bug fiend note:
		 * valgrind will complain on this line because of the
		 * data[i] on the syspage. Linux keeps a syscall page at
		 * 0xf..f600000 (vsyscall), but valgrind doesn't know this.
		 * This is safe, but will need a workaround *eventually* */
		state->write8(mmap_os, i, data[i]);
	}

	if (heap_min != ~0UL && heap_max != 0) {
		/* scanning memory is kind of stupid, but we're desperate */
		sys_model->setModelU64(kmodule->module, "heap_begin", heap_min);
		 /* max = start of last page */ 
		sys_model->setModelU64(
			kmodule->module, "heap_end", heap_max + 4096);
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
	for (unsigned int i = 0; i < len/PAGE_SIZE; i++)
		bindMappingPage(state, f, m, i);
}

void ExecutorVex::setupProcessMemory(ExecutionState* state, Function* f)
{
	std::list<GuestMem::Mapping> memmap(gs->getMem()->getMaps());
	foreach (it, memmap.begin(), memmap.end())
		bindMapping(state, f, *it);
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
	MemoryObject	*state_regctx_mo;
	ObjectState 	*state_regctx_os;
	KFunction	*kf;
	unsigned int	state_regctx_sz;
	const char	*state_data;	

	state_regctx_mo = allocRegCtx(state, f);
	es2esv(*state).setRegCtx(state_regctx_mo);

	if (SymRegs) {
		std::cerr << "[ExeVex] Making register symbolic.\n";
		state_regctx_os = makeSymbolic(*state, state_regctx_mo, "reg");
	} else
		state_regctx_os = state->bindMemObjWriteable(state_regctx_mo);

	if (symPathWriter) state->symPathOS = symPathWriter->open();
	if (statsTracker) statsTracker->framePushed(*state, 0);

	kf = kmodule->getKFunction(f);
	assert (f->arg_size() == 1);
	state->bindArgument(kf, 0, state_regctx_mo->getBaseExpr());

	state_data = (const char*)gs->getCPUState()->getStateData();

	if (SymRegs) {
		Exempts		ex(getRegExempts(gs));

		/* restore stack pointer into symregs */
		foreach (it, ex.begin(), ex.end()) {
			unsigned	off, len;

			off = it->first;
			len = it->second;
			for (unsigned i=0; i < len; i++)
				state->write8(
					state_regctx_os,
					off+i,
					state_data[off+i]);
		}

		markExit(*state, GE_IGNORE);
		return;
	}

	state_regctx_sz = gs->getCPUState()->getStateSize();
	for (unsigned int i = 0; i < state_regctx_sz; i++)
		state->write8(state_regctx_os, i, state_data[i]);
}

void ExecutorVex::run(ExecutionState &initialState)
{
	km_vex->bindModuleConstants(this);
	Executor::run(initialState);
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
	const VexSB		*vsb;

	/* need to trapeze between VSB's; depending on exit type,
	 * translate VSB exits into LLVM instructions on the fly */
	cur_func = (state.stack.back()).kf->function;

	vsb = km_vex->getVSB(cur_func);
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

	/* XXX: expensive-- lots of storage */
	reg_sz = gs->getCPUState()->getStateSize();
	crumb_base = new uint8_t[sizeof(struct breadcrumb)+(reg_sz*2)];
	crumb_buf = crumb_base;
	bc = reinterpret_cast<struct breadcrumb*>(crumb_base);

	bc_mkhdr(bc, BC_TYPE_VEXREG, 0, reg_sz*2);
	crumb_buf += sizeof(struct breadcrumb);

	/* 1. store concrete cache */
	memcpy(crumb_buf, gs->getCPUState()->getStateData(), reg_sz);
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
	GuestExitType	exit_type;

	if (LogRegs) logXferRegisters(state);

	exit_type = (GuestExitType)getExitType(state);
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
		std::cerr << "[VEXLLVM] VEX Emulation warning!?\n";
		handleXferJmp(state, ki);
		return;
	case GE_YIELD:
		std::cerr << "[VEXLLVM] Need to support yielding\n" ;
	case GE_IGNORE:
		handleXferJmp(state, ki);
		return;
	case GE_SIGSEGV:
		std::cerr << "[VEXLLVM] Caught SigSegV. Error Exit.\n";
		TERMINATE_ERROR(this,
			state,
			"VEX SIGSEGV error: jump to sigsegv",
			"sigsegv.err");
		return;

	case GE_SIGTRAP:
		std::cerr << "[VEXLLVM] Caught SigTrap. Exiting\n";
		TERMINATE_EXIT(this, state);
		return;
	default:
		std::cerr << "WTF: EXIT_TYPE=" << exit_type << '\n';
		assert (0 == 1 && "SPECIAL EXIT TYPE");
	}

	/* XXX need better bad stack frame handling */
	ref<Expr> result = MK_CONST(0, Expr::Bool);
	result = eval(ki, 0, state);

	std::cerr <<  "terminating initial stack frame\nresult: ";
	result->dump();
	TERMINATE_EXIT(this, state);
}

void ExecutorVex::handleXferJmp(ExecutionState& state, KInstruction* ki)
{
	struct XferStateIter	iter;
	xferIterInit(iter, &state, ki);
	while (xferIterNext(iter))
		jumpToKFunc(*(iter.res.first), kmodule->getKFunction(iter.f));
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
		ExecutionState	*es = iter.res.first;
		executeCall(*es, ki, iter.f, args);
		if (KeepDeadStack == false)
			es->stack.clearTail();
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
		Query		q(state.constraints, MK_CONST(1,1));

		n++;
		sprintf(prefix, "sc-%d.%d", es2esv(state).getSyscallCount(), n);
		SMTPrinter::dump(q, prefix);
	}

	es2esv(state).incSyscallCount();

	sysnr = 0;
	switch (gs->getArch()) {
#define AS_COPY(off, w)	state.addressSpace.copyToBuf(\
		es2esv(state).getRegCtx(), &sysnr, off, w);
	case Arch::X86_64: AS_COPY(
		offsetof(VexGuestAMD64State, guest_RAX), 8); break;
	case Arch::ARM: AS_COPY(
		offsetof(VexGuestARMState, guest_R7), 4); break;
	case Arch::I386: AS_COPY(
		offsetof(VexGuestX86State, guest_EAX), 4); break;
	default:
		assert (0 == 1 && "ULP");
	}

	if (ShowSyscalls)
		std::cerr << "[klee-mc] before syscall "
			<< sysnr
			<< "(?): states=" << stateManager->size()
			<< ". objs=" << state.getNumSymbolics()
			<< ". st=" << (void*)&state
			<< ". n=" << es2esv(state).getSyscallCount() << '\n';
		
	/* arg0 = regctx, arg1 = jmpptr */
	args.push_back(es2esv(state).getRegCtx()->getBaseExpr());
	args.push_back(eval(ki, 0, state));

	executeCall(state, ki, kf_scenter->function, args);
}

void ExecutorVex::handleXferReturn(
	ExecutionState& state, KInstruction* ki)
{
	struct XferStateIter	iter;
	unsigned		stack_depth;

	stack_depth = state.stack.size();
	if (!AllowNegativeStack && stack_depth == 1) {
		/* VEX call-stack is exhausted. KLEE resumes control. */
		TERMINATE_EXIT(this, state);
		return;
	}

	assert (stack_depth >= 1);

	xferIterInit(iter, &state, ki);
	while (xferIterNext(iter)) {
		ExecutionState*	new_state;

		new_state = iter.res.first;
		if (stack_depth > 1) {
			/* pop frame to represent a 'return' */
			/* if the depth < 1, treat a ret like a jump */
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
		<< "KLEE: ERROR: Calling non-special external function : "
		<< function->getName().str() << "\n";
	TERMINATE_ERROR(this, state, "externals disallowed", "user.err");
}

/* copy concrete parts into guest regs. */
void ExecutorVex::updateGuestRegs(ExecutionState& state)
{
	void	*guest_regs;
	guest_regs = gs->getCPUState()->getStateData();
	state.addressSpace.copyToBuf(es2esv(state).getRegCtx(), guest_regs);
}

void ExecutorVex::printStackTrace(
	const ExecutionState& st,
	std::ostream& os) const
{
	unsigned idx = 0;
	foreach (it, st.stack.rbegin(), st.stack.rend()) {
		const StackFrame	&sf(*it);
		Function		*f = sf.kf->function;
		const VexSB		*vsb;

		vsb = km_vex->getVSB(f);
		os << "\t#" << idx++ << " in " << f->getName().str();
		if (vsb != NULL) {
			os << " (" << gs->getName(vsb->getGuestAddr()) << ")";
		}
		os << "\n";
	}
}

std::string ExecutorVex::getPrettyName(const llvm::Function* f) const
{
	const VexSB *vsb;

	vsb = km_vex->getVSB((llvm::Function*)f);
	if (vsb != NULL)
		return gs->getName(vsb->getGuestAddr());

	return Executor::getPrettyName(f);
}

void ExecutorVex::printStateErrorMessage(
	ExecutionState& state,
	const std::string& message,
	std::ostream& os)
{
	/* TODO: get line information for state.prevPC */
	klee_message("ERROR: %s", message.c_str());

	os << "Error: " << message << "\n";

	os << "\nStack:\n";
	printStackTrace(state, os);

	if (state.prevPC && state.prevPC->getInst()) {
		raw_os_ostream	ros(os);
		ros << "problem PC:\n";
		ros << *(state.prevPC->getInst());
		ros << "\n";
	}
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

	os = GETREGOBJRO(es);
	stack_e = es.read(
		os,
		getGuest()->getCPUState()->getStackRegOff(),
		gs->getMem()->is32Bit() ? 32 : 64);
	stack_ce = dyn_cast<ConstantExpr>(stack_e);
	if (stack_ce != NULL)
		return stack_ce->getZExtValue();

	std::cerr << "warning: COULD NOT WATCH BY STACK!!!!!!\n";
	std::cerr << "symbolic stack pointer: ";
	stack_e->print(std::cerr);
	std::cerr << '\n';
	return 0;
}

llvm::Function* ExecutorVex::getFuncByAddr(uint64_t addr)
{ return km_vex->getFuncByAddr(addr); }

unsigned ExecutorVex::getExitType(const ExecutionState& state) const
{
	unsigned	exit_off;
	ref<Expr>	e;

	exit_off = gs->getCPUState()->getStateSize()-1;
	e = state.read8(GETREGOBJRO(state), exit_off);
	assert (e->getKind() == Expr::Constant);

	return (GuestExitType)(cast<ConstantExpr>(e)->getZExtValue());
}

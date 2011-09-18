#include "llvm/Target/TargetData.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Support/CommandLine.h"
#include "klee/Internal/Module/KModule.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Analysis/ProfileInfo.h"
#include "klee/Config/config.h"
#include "klee/breadcrumb.h"
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
#include "arch.h"
#include "syscall/syscallparams.h"
#include "static/Sugar.h"

#include "SyscallSFH.h"
#include "FdtSFH.h"

#include "ExecutorVex.h"
#include "ExeStateVex.h"

using namespace klee;
using namespace llvm;

extern bool WriteTraces;

#define es2esv(x)	static_cast<ExeStateVex&>(x)

namespace
{
	cl::opt<bool> SymArgs(
		"symargs",
		cl::desc("Make argument strings symbolic"),
		cl::init(false));

	cl::opt<bool> SymRegs(
		"symregs",
		cl::desc("Mark initial register file as symbolic"),
		cl::init(false));

	cl::opt<bool> LogRegs(
		"logregs",
		cl::desc("Log registers."),
		cl::init(false));

	cl::opt<bool> OptimizeModule(
		"optimize",
		cl::desc("Optimize before execution"),
		cl::init(false));

	cl::opt<bool> CheckDivZero(
		"check-div-zero",
		cl::desc("Inject checks for division-by-zero"),
		cl::init(false));

	cl::opt<bool> ConcreteVfs(
		"concrete-vfs",
		cl::desc("Treat absolute path opens as concrete"),
		cl::init(false));

	cl::opt<bool> UseFDT(
		"use-fdt",
		cl::desc("Use TJ's FDT model"),
		cl::init(false));
}

ExecutorVex::ExecutorVex(
	const InterpreterOptions &opts,
	InterpreterHandler *ih,
	Guest	*in_gs)
: Executor(opts, ih)
, gs(in_gs)
, native_code_bytes(0)
{
	assert (kmodule == NULL && "KMod already initialized? My contract!");

	/* XXX TODO: module flags */
	llvm::sys::Path LibraryDir(KLEE_DIR "/" RUNTIME_CONFIGURATION "/lib");
	Interpreter::ModuleOptions mod_opts(
		LibraryDir.c_str(),
		OptimizeModule,
		CheckDivZero,
		std::vector<std::string>());

	assert (gs);

	if (!theGenLLVM) theGenLLVM = new GenLLVM(in_gs);
	if (!theVexHelpers) theVexHelpers = VexHelpers::create(Arch::X86_64);

	std::cerr << "[klee-mc] Forcing fake vsyspage reads\n";
	theGenLLVM->setFakeSysReads();

	theVexHelpers->loadUserMod(
		(UseFDT) ?
			"libkleeRuntimeMC-fdt.bc" :
			"libkleeRuntimeMC.bc");

	xlate = new VexXlate(Arch::X86_64);
	xlate_cache = new VexFCache(xlate);
	kmodule = new KModule(theGenLLVM->getModule());

	target_data = kmodule->targetData;

	// Initialize the context.
	assert(target_data->isLittleEndian() && "BIGENDIAN??");

	Context::initialize(
		target_data->isLittleEndian(),
		(Expr::Width) target_data->getPointerSizeInBits());

	sfh = (UseFDT) ? new FdtSFH(this) : new SyscallSFH(this);

	sfh->prepare();
	kmodule->prepare(mod_opts, ih);

	if (StatsTracker::useStatistics())
		statsTracker = new StatsTracker(
			*this,
			kmodule,
			interpreterHandler->getOutputFilename("assembly.ll"),
			mod_opts.ExcludeCovFiles,
			userSearcherRequiresMD2U());
}

ExecutorVex::~ExecutorVex(void)
{
	delete xlate_cache;
	delete xlate;
	if (sfh) delete sfh;
	sfh = NULL;
	if (kmodule) delete kmodule;
	kmodule = NULL;
}

void ExecutorVex::runImage(void)
{
	ExecutionState	*state;
	Function	*init_func;
	KFunction	*init_kfunc;
	bool		is_new;

	// force deterministic initialization of memory objects
	srand(1);
	srandom(1);

	// acrobatics because we have a fucking circular dependency
	// on the globaladdress stucture which keeps us from binding
	// the module constant table.
	//
	// This is mainly a problem for check-div-zero, since it won't
	// yet have a global address but binding the constant table
	// requires it!
	init_func = getFuncByAddrNoKMod((uint64_t)gs->getEntryPoint(), is_new);
	assert (init_func != NULL && "Could not get init_func. Bad decode?");
	if (init_func == NULL) {
		fprintf(stderr, "[klee-mc] COULD NOT GET INIT_FUNC\n");
		return;
	}

	/* add modules before initializing globals so that everything
	 * will link in properly */
	std::list<Module*> l = theVexHelpers->getModules();
	foreach (it, l.begin(), l.end()) {
		kmodule->addModule(*it);
	}

	if (UseFDT) prepFDT(init_func);

	init_kfunc = kmodule->addFunction(init_func);

	statsTracker->addKFunction(init_kfunc);
	bindKFuncConstants(init_kfunc);

	state = ExeStateVex::make(kmodule->getKFunction(init_func));

	prepState(state, init_func);
	initializeGlobals(*state);

	sfh->bind();
	kf_scenter = kmodule->getKFunction("sc_enter");
	assert (kf_scenter && "Could not load sc_enter from runtime library");


	if (SymArgs) makeArgsSymbolic(state);

	processTree = new PTree(state);
	state->ptreeNode = processTree->root;

	fprintf(stderr, "COMMENCE THE RUN!!!!!!\n");
	run(*state);
	fprintf(stderr, "DONE RUNNING.\n");

	delete processTree;
	processTree = 0;

	// hack to clear memory objects
	delete memory;
	memory = new MemoryManager();

	globalObjects.clear();
	globalAddresses.clear();

	if (statsTracker) statsTracker->done();

	fprintf(stderr, "OK.\n");
}

void ExecutorVex::initializeGlobals(ExecutionState &state)
{
	Module *m;

	m = kmodule->module;

	assert (m->getModuleInlineAsm() == "" && "No inline asm!");
	assert (m->lib_begin() == m->lib_end() &&
		"XXX do not support dependent libraries");

	initGlobalFuncs();

	// allocate and initialize globals, done in two passes since we may
	// need address of a global in order to initialize some other one.

	// allocate memory objects for all globals
	foreach (i, m->global_begin(), m->global_end()) {
		if (i->isDeclaration()) allocGlobalVariableDecl(state, *i);
		else allocGlobalVariableNoDecl(state, *i);
	}

	// link aliases to their definitions (if bound)
	foreach (i, m->alias_begin(), m->alias_end()) {
		// Map the alias to its aliasee's address.
		// This works because we have addresses for everything,
		// even undefined functions.
		globalAddresses.insert(
			std::make_pair(i, evalConstant(i->getAliasee())));
	}

	// once all objects are allocated, do the actual initialization
	foreach (i, m->global_begin(), m->global_end()) {
		MemoryObject		*mo;
		const ObjectState	*os;
		ObjectState		*wos;

		if (!i->hasInitializer()) continue;

		mo = globalObjects.find(i)->second;
		os = state.addressSpace.findObject(mo);

		assert(os);

		wos = state.addressSpace.getWriteable(mo, os);

		initializeGlobalObject(state, wos, i->getInitializer(), 0);
		// if (i->isConstant()) os->setReadOnly(true);
	}

	std::cerr << "GLOBAL ADDRESSES size=" << globalAddresses.size() << '\n';
}

void ExecutorVex::allocGlobalVariableNoDecl(
	ExecutionState& state,
	const GlobalVariable& gv)
{
	const Type	*ty;
	uint64_t	size;
	MemoryObject	*mo = 0;
	ObjectState	*os;

	ty = gv.getType()->getElementType();
	size = target_data->getTypeStoreSize(ty);

	assert (gv.getName()[0] !='\01');

	if (!mo) mo = memory->allocate(size, false, true, &gv, &state);
	assert(mo && "out of memory");

	os = state.bindMemObj(mo);
	globalObjects.insert(std::make_pair(&gv, mo));
	globalAddresses.insert(std::make_pair(&gv, mo->getBaseExpr()));

	if (!gv.hasInitializer()) os->initializeToRandom();
}

void ExecutorVex::initGlobalFuncs(void)
{
	Module *m = kmodule->module;

	// represent function globals using the address of the actual llvm function
	// object. given that we use malloc to allocate memory in states this also
	// ensures that we won't conflict. we don't need to allocate a memory object
	// since reading/writing via a function pointer is unsupported anyway.
	foreach (i, m->begin(), m->end()) {
		Function *f = i;
		ref<ConstantExpr> addr(0);

		// If the symbol has external weak linkage then it is implicitly
		// not defined in this module; if it isn't resolvable then it
		// should be null.
		assert (!f->hasExternalWeakLinkage());
		addr = Expr::createPointer((unsigned long) (void*) f);
		globalAddresses.insert(std::make_pair(f, addr));
	}
}

//TODO: declare in kmodule h
Function *getStubFunctionForCtorList(
	Module *m,
	GlobalVariable *gv,
	std::string name);

void ExecutorVex::prepFDT(Function *init_func)
{
	GlobalVariable *ctors = kmodule->module->getNamedGlobal("llvm.global_ctors");
	std::cerr << "checking for global ctors and dtors" << std::endl;
	if (ctors) {
		std::cerr << "installing ctors" << std::endl;
		Function* ctorStub;

		ctorStub = getStubFunctionForCtorList(
			kmodule->module, ctors, "klee.ctor_stub");
		kmodule->addFunction(ctorStub);
		CallInst::Create(
			ctorStub,
			"",
			init_func->begin()->begin());
	}
	// can't install detours because this function returns almost immediately... todo
	// GlobalVariable *dtors = kmodule->module->getNamedGlobal("llvm.global_dtors");
	// do them later
	// if (dtors) {
	// 	std::cerr << "installing dtors" << std::endl;
	// 	Function *dtorStub;
	// 	dtorStub = getStubFunctionForCtorList(kmodule->module, dtors, "klee.dtor_stub");
	// 	kmodule->addFunction(dtorStub);
	// 	foreach (it, init_func->begin(), init_func->end()) {
	// 		if (!isa<ReturnInst>(it->getTerminator())) continue;
	// 		CallInst::Create(dtorStub, "", it->getTerminator());
	// 	}
	// }

	const IntegerType* boolType = IntegerType::get(getGlobalContext(), 1);
	GlobalVariable* concrete_vfs =
		static_cast<GlobalVariable*>(kmodule->module->
			getGlobalVariable("concrete_vfs", boolType));

	concrete_vfs->setInitializer(ConcreteVfs ?
		ConstantInt::getTrue(getGlobalContext()) :
		ConstantInt::getFalse(getGlobalContext()));
	concrete_vfs->setConstant(true);
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

/* Argh, really need to stop macroing the page size all the time */
#define PAGE_SIZE	4096
void ExecutorVex::bindMappingPage(
	ExecutionState* state,
	Function* f,
	const GuestMem::Mapping& m,
	unsigned int pgnum)
{
	const char		*data;
	MemoryObject		*mmap_mo;
	ObjectState		*mmap_os;

	assert (m.getBytes() > pgnum*PAGE_SIZE);
	assert ((m.getBytes() % PAGE_SIZE) == 0);
	assert ((m.offset.o & (PAGE_SIZE-1)) == 0);

	mmap_mo = memory->allocateFixed(
		((uint64_t)gs->getMem()->getData(m))+(PAGE_SIZE*pgnum),
		PAGE_SIZE,
		f->begin()->begin(),
		state);

	if (m.type == GuestMem::Mapping::STACK) {
		mmap_mo->setName("stack");
	} else {
		mmap_mo->setName("guestimg");
	}

	data = (const char*)gs->getMem()->getData(m) + pgnum*PAGE_SIZE;
	mmap_os = state->bindMemObj(mmap_mo);
	for (unsigned int i = 0; i < PAGE_SIZE; i++) {
		/* bug fiend note:
		 * valgrind will complain on this line because of the
		 * data[i] on the syspage. Linux keeps a syscall page at
		 * 0xf..f600000 (vsyscall), but valgrind doesn't know this.
		 * This is safe, but will need a workaround *eventually* */
		state->write8(mmap_os, i, data[i]);
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

	/* !cached => put in cache, alert kmodule, other bookkepping */
	f = xlate_cache->getFunc(host_addr, guest_ptr(guest_addr));
	if (f == NULL) return NULL;

	/* need to know func -> vsb to compute func's guest address */
	vsb = xlate_cache->getCachedVSB(guest_ptr(guest_addr));
	assert (vsb && "Dropped VSB too early?");
	func2vsb_table[(uint64_t)f] = vsb;

	is_new = true;
	native_code_bytes += vsb->getEndAddr() - vsb->getGuestAddr();

	return f;
}


Function* ExecutorVex::getFuncByAddr(uint64_t guest_addr)
{
	KFunction	*kf;
	Function	*f;
	bool		is_new;

	f = getFuncByAddrNoKMod(guest_addr, is_new);
	if (f == NULL) return NULL;
	if (!is_new) return f;

	/* stupid kmodule stuff */
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
	state_regctx_os = getRegObj(state);
	state.write8(
		state_regctx_os,
		gs->getCPUState()->getExitTypeOffset(),
		v);
}

ObjectState* ExecutorVex::getRegObj(ExecutionState& state)
{
	MemoryObject	*mo;
	mo = es2esv(state).getRegCtx();
	return state.addressSpace.findObject(mo);
}

void ExecutorVex::logXferRegisters(ExecutionState& state)
{
	ObjectState*		state_regctx_os;
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
	state_regctx_os = getRegObj(state);
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

void ExecutorVex::xferIterInit(
	struct XferStateIter& iter,
	ExecutionState* state,
	KInstruction* ki)
{
	iter.v = eval(ki, 0, *state).value;
	iter.free = state;
	iter.first = true;
}

/* this is mostly a copy of Executor's implementation of
 * the symbolic function call; probably should try to merge the two */
bool ExecutorVex::xferIterNext(struct XferStateIter& iter)
{
	Function		*iter_f;
	ref<ConstantExpr>	value;
	uint64_t		addr;
	bool			success;

	iter_f = NULL;
	while (1) {
		if (iter.free == NULL) return false;

		success = solver->getValue(*(iter.free), iter.v, value);
		assert(success && "FIXME: Unhandled solver failure");
		(void) success;

		iter.res = fork(*(iter.free), EqExpr::create(iter.v, value), true);
		iter.first = false;
		iter.free = iter.res.second;

		if (!iter.res.first) continue;

		addr = value->getZExtValue();
		iter_f = getFuncByAddr(addr);
		if (iter_f == NULL) {
			fprintf(stderr, "bogus jmp to %p!\n", (void*)addr);
			terminateStateOnError(
				*(iter.res.first),
				"fork error: jumping to bad pointer",
				"badjmp.err");
			continue;
		}

		break;
	}

	assert (iter_f != NULL && "BAD FUNCTION TO JUMP TO");
	iter.f = iter_f;
	iter.f_addr = addr;

	// (iter.res.second || !iter.first) => non-unique resolution
	// normal klee cares about this, we don't

	return true;
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
	CallPathNode	*cpn;
	MemoryObject	*regctx_mo;
	KInstIterator	ki = state.getCaller();

	assert (kf != NULL);
	assert (state.stack.size() > 0);

	/* save, pop off old state */
	regctx_mo = es2esv(state).getRegCtx();
	cpn = state.stack.back().callPathNode;
	state.popFrame();

	/* create new frame to replace old frame;
	   new frame initialized with target function kf */
	state.pushFrame(ki, kf);
	StackFrame	&sf = state.stack.back();
	sf.callPathNode = cpn;

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

	uint64_t	sysnr;
	state.addressSpace.copyToBuf(es2esv(state).getRegCtx(), &sysnr, 0, 8);
	fprintf(stderr, "before syscall %d(?): states=%d. objs=%d. st=%p\n",
		(int)sysnr,
		stateManager->size(),
		state.getNumSymbolics(),
		(void*)&state);

	/* arg0 = regctx, arg1 = jmpptr */
	args.push_back(es2esv(state).getRegCtx()->getBaseExpr());
	args.push_back(eval(ki, 0, state).value);

	executeCall(state, ki, kf_scenter->function, args);

	fprintf(stderr, "syscall queued.\n");
}

void ExecutorVex::handleXferReturn(
	ExecutionState& state, KInstruction* ki)
{
	struct XferStateIter	iter;

	assert (state.stack.size() > 1);

	xferIterInit(iter, &state, ki);
	while (xferIterNext(iter)) {
		ExecutionState*	new_state;
		new_state = iter.res.first;
		new_state->popFrame();
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
	if (sfh->handle(state, function, target, arguments)) {
		return;
	}

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
	os << state.addressSpace.objects;

	os << "\nRegisters: \n";
	gs->getCPUState()->print(os);

	unsigned idx = 0;
	os << "\nStack: \n";
	foreach (it, state.stack.rbegin(), state.stack.rend())
	{
		StackFrame	&sf = *it;
		Function	*f = sf.kf->function;
		VexSB		*vsb;

		vsb = func2vsb_table[(uint64_t)f];
		os << "\t#" << idx++ << " in " << f->getNameStr();
		if (vsb) {
			os << " (" << gs->getName(vsb->getGuestAddr()) << ")";
		}
		os << "\n";
	}

	if (state.prevPC && state.prevPC->inst) {
		raw_os_ostream	ros(os);
		ros << "problem PC:\n";
		ros << *(state.prevPC->inst);
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

#include "llvm/Target/TargetData.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Support/CommandLine.h"
#include "klee/Internal/Module/KModule.h"
#include "llvm/System/Path.h"
#include "klee/Config/config.h"
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
#include "ExecutorVex.h"

/* CHEAT */
extern "C"
{
#include "valgrind/libvex_guest_amd64.h"
}


using namespace klee;
using namespace llvm;

extern bool WriteTraces;

namespace
{
  cl::opt<bool>
  LogRegs("logregs",
  	cl::desc("Log registers."),
	cl::init(false));
}

ExecutorVex::ExecutorVex(
	const InterpreterOptions &opts,
	InterpreterHandler *ih,
	Guest	*in_gs)
: Executor(opts, ih)
, gs(in_gs)
{
	assert (kmodule == NULL && "KMod already initialized? My contract!");

	/* XXX TODO: module flags */
	llvm::sys::Path LibraryDir(KLEE_DIR "/" RUNTIME_CONFIGURATION "/lib");
	Interpreter::ModuleOptions mod_opts(
		LibraryDir.c_str(),
		false,
		false,
		std::vector<std::string>());

	assert (gs);

	if (!theGenLLVM) theGenLLVM = new GenLLVM(in_gs);
	if (!theVexHelpers) theVexHelpers = new VexHelpers(Arch::X86_64);

	theVexHelpers->loadUserMod("libkleeRuntimeMC.bc");

	xlate = new VexXlate(Arch::X86_64);
	xlate_cache = new VexFCache(xlate);
	kmodule = new KModule(theGenLLVM->getModule());

	target_data = kmodule->targetData;
	dbgStopPointFn = kmodule->dbgStopPointFn;

	// Initialize the context.
	assert(target_data->isLittleEndian() && "BIGENDIAN??");

	Context::initialize(
		target_data->isLittleEndian(),
		(Expr::Width) target_data->getPointerSizeInBits());

	sfh = new SyscallSFH(this);

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

	// force deterministic initialization of memory objects
	srand(1);
	srandom(1);

	init_func = getFuncByAddr((uint64_t)gs->getEntryPoint());
	state = new ExecutionState(kmodule->getKFunction(init_func));

	/* important to add modules before intializing globals */
	std::list<Module*> l = theVexHelpers->getModules();
	foreach (it, l.begin(), l.end()) {
		kmodule->addModule(*it);
	}

	prepState(state, init_func);
	initializeGlobals(*state);

	sfh->bind();
	kf_scenter = kmodule->getKFunction("sc_enter");
	assert (kf_scenter && "Could not load sc_enter from runtime library");

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

void ExecutorVex::prepState(ExecutionState* state, Function* f)
{
	setupRegisterContext(state, f);
	setupProcessMemory(state, f);
}

#define STACK_EXTEND 0x100000
void ExecutorVex::bindMapping(
	ExecutionState* state,
	Function* f,
	GuestMem::Mapping m)
{
	MemoryObject		*mmap_mo;
	ObjectState		*mmap_os;
	unsigned int		len;
	unsigned int		copy_offset;
	const char		*data;

	len = m.getBytes();
	if (m.isStack()) {
		mmap_mo = memory->allocateFixed(
			((uint64_t)m.offset.o)-STACK_EXTEND,
			len+STACK_EXTEND,
			f->begin()->begin(),
			state);
		copy_offset = STACK_EXTEND;
		mmap_mo->setName("stack");
	} else {
		mmap_mo = memory->allocateFixed(
			((uint64_t)m.offset.o),
			len,
			f->begin()->begin(),
			state);
		mmap_mo->setName("guestimg");
		copy_offset = 0;
	}

	data = (const char*)gs->getMem()->getData(m);
	mmap_os = state->bindMemObj(mmap_mo);
	for (unsigned int i = 0; i < len; i++) {
		/* bug fiend note:
		 * valgrind will complain on this line because of the
		 * data[i] on the syspage. Linux keeps a syscall page at
		 * 0xf..f600000 (vsyscall), but valgrind doesn't know this.
		 * This is safe, but will need a workaround *eventually* */
		state->write8(mmap_os, i+copy_offset, data[i]);
	}

	mmap_mo->print(std::cerr);
	std::cerr << "\n";
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
	state_regctx_sz = gs->getCPUState()->getStateSize();

	if (symPathWriter) state->symPathOS = symPathWriter->open();
	if (statsTracker) statsTracker->framePushed(*state, 0);

	kf = kmodule->getKFunction(f);

	assert (f->arg_size() == 1);
	state->bindArgument(kf, 0, state_regctx_mo->getBaseExpr());

	state_regctx_os = state->bindMemObj(state_regctx_mo);

	const char*  state_data = (const char*)gs->getCPUState()->getStateData();
	for (unsigned int i=0; i < state_regctx_sz; i++)
		state->write8(state_regctx_os, i, state_data[i]);

	state->setRegCtx(state_regctx_mo);
}

void ExecutorVex::run(ExecutionState &initialState)
{
	bindModuleConstants();
	Executor::run(initialState);
}

Function* ExecutorVex::getFuncByAddr(uint64_t guest_addr)
{
	uint64_t	host_addr;
	KFunction	*kf;
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

	/* XXX */
	host_addr = guest_addr;

	/* cached => already seen it */
	f = xlate_cache->getCachedFunc(guest_ptr(guest_addr));
	if (f != NULL) return f;

	/* !cached => put in cache, alert kmodule, other bookkepping */
	f = xlate_cache->getFunc((void*)host_addr, guest_ptr(guest_addr));
	if (f == NULL) return NULL;

	/* wipe out intrinsics which make klee puke */
	/* FIXME: this should be an LLVM function pass done in the cache
	 * code. */
	/* go through all basic blocks */
	foreach (it, f->begin(), f->end()) {
		/* go through all instructions for BB 'it' */
		BasicBlock::iterator	ins, ins_end;
		ins_end = (*it).end();
		for (ins = (*it).begin(); ins != ins_end; ) {
			IntrinsicInst	*ii;

			ii = dyn_cast<IntrinsicInst>(&*ins);
			ins++;

			if (!ii) continue;

			if (ii->getIntrinsicID() == Intrinsic::memory_barrier)
				ii->eraseFromParent();
		}
	}

	/* need to know func -> vsb to compute func's guest address */
	vsb = xlate_cache->getCachedVSB(guest_ptr(guest_addr));
	assert (vsb && "Dropped VSB too early?");
	func2vsb_table[(uint64_t)f] = vsb;

	/* stupid kmodule stuff */
	kf = kmodule->addFunction(f);
	statsTracker->addKFunction(kf);
	bindKFuncConstants(kf);
	bindModuleConstTable(); /* XXX slow */

	return f;
}

void ExecutorVex::executeInstruction(
	ExecutionState &state, KInstruction *ki)
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

	handleXfer(state, ki);
}

void ExecutorVex::markExitIgnore(ExecutionState& state)
{
	ObjectState		*state_regctx_os;

	gs->getCPUState()->setExitType(GE_IGNORE);
	state_regctx_os = getRegObj(state);
	state.write8(
		state_regctx_os,
		gs->getCPUState()->getExitTypeOffset(),
		GE_IGNORE);
}

ObjectState* ExecutorVex::getRegObj(ExecutionState& state)
{
	MemoryObject	*mo;
	mo = state.getRegCtx();
	return state.addressSpace.findObject(mo);
}

extern "C" {
#include <valgrind/libvex_guest_amd64.h>
}

void ExecutorVex::logXferRegisters(ExecutionState& state)
{
	ObjectState*	state_regctx_os;
	uint8_t*	concrete_mask;
	unsigned int	reg_sz;

	if (!LogRegs) return;

	/* XXX: expensive-- lots of storage */
	reg_sz = gs->getCPUState()->getStateSize();

	/* 1. store concrete cache */
	state.recordRegisters(gs->getCPUState()->getStateData(), reg_sz);

	/* 2. store concrete mask */
	state_regctx_os = getRegObj(state);
	concrete_mask = new uint8_t[reg_sz];
	for (unsigned int i = 0; i < reg_sz; i++) {
		concrete_mask[i] = 0;
		if (state_regctx_os->isByteConcrete(i))
			concrete_mask[i] = 0xff;
	}
	state.recordRegisters(concrete_mask, reg_sz);

	delete [] concrete_mask;
}

/* handle transfering between VSB's */
void ExecutorVex::handleXfer(ExecutionState& state, KInstruction *ki)
{
	GuestExitType		exit_type;

	updateGuestRegs(state);
	logXferRegisters(state);

	exit_type = gs->getCPUState()->getExitType();
	markExitIgnore(state);

	switch(exit_type) {
	case GE_CALL:
		handleXferCall(state, ki);
		return;
	case GE_RETURN:
		handleXferReturn(state, ki);
		return;
	case GE_SYSCALL: {
		handleXferSyscall(state, ki);
		return;
	}
	case GE_EMWARN:
		std::cerr << "[VEXLLVM] VEX Emulation warning!?" << std::endl;
	case GE_IGNORE:
		handleXferJmp(state, ki);
		return;
	default:
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
			fprintf(stderr, "bogus jmp to %p!\n", addr);
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
	int			k = 0;
	xferIterInit(iter, &state, ki);
	while (xferIterNext(iter)) {
		jumpToKFunc(*(iter.res.first), kmodule->getKFunction(iter.f));
		k++;
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
	regctx_mo = state.getRegCtx();
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
 * jump.  g=f(x) => g(x) -> f(x). (g directly follows f) */
void ExecutorVex::handleXferCall(ExecutionState& state, KInstruction* ki)
{
	std::vector< ref<Expr> > 	args;
	struct XferStateIter		iter;

	args.push_back(state.getRegCtx()->getBaseExpr());
	xferIterInit(iter, &state, ki);
	while (xferIterNext(iter)) {
		executeCall(*(iter.res.first), ki, iter.f, args);
	}
}

/**
 * Pass return address and register context to sc handler.
 * SC handler has additional special funcs for manipulating
 * the register context.
 */
void ExecutorVex::handleXferSyscall(
	ExecutionState& state, KInstruction* ki)
{
	std::vector< ref<Expr> > 	args;
	struct XferStateIter		iter;

	fprintf(stderr, "before syscall: states=%d\n", stateManager->size());

	/* arg0 = regctx */
	args.push_back(state.getRegCtx()->getBaseExpr());
	/* arg1 = jmpptr */
	args.push_back(eval(ki, 0, state).value);

	executeCall(state, ki, kf_scenter->function, args);

	fprintf(stderr, "after syscall: states=%d. state=%p\n",
		stateManager->size(), &state);
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
	state.addressSpace.copyToBuf(state.getRegCtx(), guest_regs);
}

void ExecutorVex::initializeGlobals(ExecutionState &state)
{
	Module *m;

	m = kmodule->module;

	assert (m->getModuleInlineAsm() == "" && "No inline asm!");
	assert (m->lib_begin() == m->lib_end() &&
		"XXX do not support dependent libraries");

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

	fprintf(stderr, "GLOBAL ADDRESSES size=%d\n", globalAddresses.size());
}

void ExecutorVex::allocGlobalVariableDecl(
	ExecutionState& state,
	const GlobalVariable& gv)
{
	assert(0 == 1 && "STUB");
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

void ExecutorVex::makeSymbolicTail(
	ExecutionState& state,
	const MemoryObject* mo,
	unsigned taken,
	const char* name)
{
	ObjectState	*os;
	MemoryObject	*mo_head, *mo_tail;
	char		*buf_head;
	uint64_t	mo_addr, mo_size, head_size;

	mo_addr = mo->address;
	mo_size = mo->size;

	assert (mo_size > taken && "Off+Taken out of range");

	/* copy buffer data */
	head_size = mo_size - taken;
	buf_head = new char[head_size];
	state.addressSpace.copyToBuf(mo, buf_head, 0, head_size);
	os = state.addressSpace.findObject(mo);

	/* free object from address space */
	state.unbindObject(mo);

	/* mark head concrete */
	mo_head = memory->allocateFixed(mo_addr, head_size, 0, &state);
	os = state.bindMemObj(mo_head);
	for(unsigned i = 0; i < head_size; i++) state.write8(os, i, buf_head[i]);

	/* mark tail symbolic */
	mo_tail = memory->allocateFixed(mo_addr+head_size, taken, 0, &state);
	executeMakeSymbolic(
		state,
		mo_tail,
		ConstantExpr::alloc(taken, 32),
		name);


	delete [] buf_head;
}

void ExecutorVex::makeSymbolicHead(
	ExecutionState& state,
	const MemoryObject* mo,
	unsigned taken,
	const char* name)
{
	ObjectState	*os;
	MemoryObject	*mo_head, *mo_tail;
	char		*buf_tail;
	uint64_t	mo_addr, mo_size, tail_size;

	mo_addr = mo->address;
	mo_size = mo->size;

	if (mo_size == taken) {
		executeMakeSymbolic(
			state,
			mo,
			ConstantExpr::alloc(mo_size, 32),
			name);
		return;
	}

	assert (mo_size > taken && "Off+Taken out of range");

	/* copy buffer data */
	tail_size = mo_size - taken;
	buf_tail = new char[tail_size];
	state.addressSpace.copyToBuf(mo, buf_tail, taken, tail_size);
	os = state.addressSpace.findObject(mo);

	/* free object from address space */
	state.unbindObject(mo);

	mo_head = memory->allocateFixed(mo_addr, taken, 0, &state);
	executeMakeSymbolic(
		state,
		mo_head,
		ConstantExpr::alloc(taken, 32),
		name);

	mo_tail = memory->allocateFixed(
		mo_addr+taken, tail_size, 0, &state);
	os = state.bindMemObj(mo_tail);
	for(unsigned i = 0; i < tail_size; i++) state.write8(os, i, buf_tail[i]);

	delete [] buf_tail;
}

void ExecutorVex::makeSymbolicMiddle(
	ExecutionState& state,
	const MemoryObject* mo,
	unsigned mo_off,
	unsigned taken,
	const char* name)
{
	ObjectState	*os;
	MemoryObject	*mo_head, *mo_tail, *mo_mid;
	char		*buf_head, *buf_tail;
	uint64_t	mo_addr, mo_size, tail_size;

	mo_addr = mo->address;
	mo_size = mo->size;
	assert (mo_size > (mo_off+taken) && "Off+Taken out of range");

	/* copy buffer data */
	buf_head = new char[mo_off];
	tail_size = mo_size - (mo_off + taken);
	buf_tail = new char[tail_size];
	state.addressSpace.copyToBuf(mo, buf_head, 0, mo_off);
	state.addressSpace.copyToBuf(mo, buf_tail, mo_off+taken, tail_size);
	os = state.addressSpace.findObject(mo);

	/* free object from address space */
	state.unbindObject(mo);

	mo_head = memory->allocateFixed(mo_addr, mo_off, NULL, &state);

	os = state.bindMemObj(mo_head);
	for(unsigned i = 0; i < mo_off; i++) state.write8(os, i, buf_head[i]);

	mo_mid = memory->allocateFixed(mo_addr+mo_off, taken, NULL, &state);
	mo_mid->setName(name);
	executeMakeSymbolic(
		state,
		mo_mid,
		ConstantExpr::alloc(taken, 32),
		name);

	mo_tail = memory->allocateFixed(
		mo_addr+mo_off+taken, tail_size, 0, &state);
	os = state.bindMemObj(mo_tail);
	for(unsigned i = 0; i < tail_size; i++) state.write8(os, i, buf_tail[i]);

	delete [] buf_head;
	delete [] buf_tail;
}

void ExecutorVex::makeRangeSymbolic(
	ExecutionState& state,
	void* addr,
	unsigned sz,
	const char* name)
{
	uint64_t	cur_addr;
	unsigned	total_sz;

	fprintf(stderr, "MAKE RANGE SYMBOLIC: %p-%p\n", addr, (char*)addr+sz);

	cur_addr = (uint64_t)addr;
	total_sz = 0;
	/* handle disjoint addresses */
	while (total_sz < sz) {
		const MemoryObject	*mo;
		ObjectState		*os;
		unsigned int		mo_off;
		unsigned int		tail_take_bytes;
		unsigned int		take_remaining;
		unsigned int		taken;

		mo = state.addressSpace.resolveOneMO(cur_addr);
		if (mo == NULL) {
			state.addressSpace.print(std::cerr);
			fprintf(stderr,
				"couldn't find %p in range %p-%p (state=%p)\n",
				cur_addr,
				addr, addr+sz,
				&state);
			assert ("TODO: Allocate memory");
		}

		assert (mo->address <= cur_addr && "BAD SEARCH?");
		mo_off = cur_addr - mo->address;
		assert (mo_off < mo->size && "Out of range of MO??");

		take_remaining = sz - total_sz;
		tail_take_bytes = mo->size - mo_off;
		if (mo_off > 0) {
			if (tail_take_bytes <= take_remaining) {
				/* take is excess of length of MO
				 * Chop off all the tail of the MO */
				taken = tail_take_bytes;
				makeSymbolicTail(state, mo, taken, name);
			} else {
				taken = take_remaining;
				makeSymbolicMiddle(
					state,
					mo,
					mo_off,
					taken,
					name);
			}
		} else {
			taken = (take_remaining >= tail_take_bytes) ?
					tail_take_bytes :
					take_remaining;
			makeSymbolicHead(state, mo, taken, name);
		}

		/* set stat structure as symbolic */
		cur_addr += taken;
		total_sz += taken;
	}
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

	os << "\nStack: \n";

	unsigned idx = 0;
	foreach (it, state.stack.rbegin(), state.stack.rend())
	{
		StackFrame	&sf = *it;
		Function	*f = sf.kf->function;
		VexSB		*vsb;

		vsb = func2vsb_table[(uint64_t)f];
		os	<< "\t#" << idx++
			<< " in " << f->getNameStr();
		if (vsb) {
			os	<< " ("
				<< gs->getName(vsb->getGuestAddr())
				<< ")";
		}
		os << "\n";
	}

	if (state.prevPC && state.prevPC->inst) {
		os << "problem PC:\n";
		state.prevPC->inst->print(os);
		os << "\n";
	}

	top_f = state.stack.back().kf->function;
	if (top_f) {
		os << "Func: ";
		top_f->print(os);
		os << "\n";
	}

	os << "Constraints: \n";
	state.constraints.print(os);
}

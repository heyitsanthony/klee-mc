#include "llvm/Target/TargetData.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Support/CommandLine.h"
#include "klee/Internal/Module/KModule.h"
#include "llvm/System/Path.h"
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
}

ExecutorVex::ExecutorVex(
	const InterpreterOptions &opts,
	InterpreterHandler *ih,
	Guest	*in_gs)
: ExecutorBC(opts, ih)
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

	theVexHelpers->loadUserMod("libkleeRuntimeMC.bc");

	xlate = new VexXlate(Arch::X86_64);
	xlate_cache = new VexFCache(xlate);
	kmodule = new KModule(theGenLLVM->getModule());

	target_data = kmodule->targetData;

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
//TODO: declare in kmodule h
Function *getStubFunctionForCtorList(
	Module *m,
	GlobalVariable *gv,
	std::string name);
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
	
	/* add modules before initializing globals so that everything
	 * will link in properly */
	std::list<Module*> l = theVexHelpers->getModules();
	foreach (it, l.begin(), l.end()) {
		kmodule->addModule(*it);
	}

	GlobalVariable *ctors = kmodule->module->getNamedGlobal("llvm.global_ctors");
	GlobalVariable *dtors = kmodule->module->getNamedGlobal("llvm.global_dtors");
	std::cerr << "checking for global ctors and dtors" << std::endl;
	if (ctors)
		std::cerr << "installing ctors" << std::endl;
		Function* ctorStub = getStubFunctionForCtorList(kmodule->module, ctors, "klee.ctor_stub");
		kmodule->addFunction(ctorStub);
		CallInst::Create(
			ctorStub,
			"",
			init_func->begin()->begin());
	// can't install detours because this function returns almost immediately... todo
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

	
	init_kfunc = kmodule->addFunction(init_func);
	statsTracker->addKFunction(init_kfunc);
	bindKFuncConstants(init_kfunc);

	state = ExeStateVex::make(kmodule->getKFunction(init_func));

	initializeGlobals(*state);
	prepState(state, init_func);

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

void ExecutorVex::makeArgsSymbolic(ExecutionState* state)
{
	std::vector<guest_ptr>	argv;

	argv = gs->getArgvPtrs();
	if (argv.size() == 0) return;

	fprintf(stderr,
		"[klee-mc] Making %u arguments symbolic\n",
		argv.size()-1);
	foreach (it, argv.begin()+1, argv.end()) {
		guest_ptr	p = *it;
		fprintf(stderr, "MAKE IT HAPPEN %p, sz=%d\n",
			p.o, gs->getMem()->strlen(p));
		sfh->makeRangeSymbolic(
			*state, (void*)p.o, gs->getMem()->strlen(p), "argv");
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
		((uint64_t)m.offset.o)+PAGE_SIZE*pgnum,
		PAGE_SIZE,
		f->begin()->begin(),
		state);

	if (m.isStack()) {
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

	mmap_mo->print(std::cerr);
	std::cerr << "\n";
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

	es2esv(*state).setRegCtx(state_regctx_mo);
}

void ExecutorVex::run(ExecutionState &initialState)
{
	bindModuleConstants();
	Executor::run(initialState);
}

Function* ExecutorVex::getFuncByAddrNoKMod(uint64_t guest_addr, bool& is_new)
{
	uint64_t	host_addr;
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
	if (f != NULL) {
		is_new = false;
		return f;
	}

	/* !cached => put in cache, alert kmodule, other bookkepping */
	f = xlate_cache->getFunc((void*)host_addr, guest_ptr(guest_addr));
	if (f == NULL) return NULL;

	/* need to know func -> vsb to compute func's guest address */
	vsb = xlate_cache->getCachedVSB(guest_ptr(guest_addr));
	assert (vsb && "Dropped VSB too early?");
	func2vsb_table[(uint64_t)f] = vsb;

	is_new = true;
	native_code_bytes += vsb->getEndAddr() - vsb->getGuestAddr();

	return f;
}


static bool xxx_debug = false;

Function* ExecutorVex::getFuncByAddr(uint64_t guest_addr)
{
	Function	*ef;
	KFunction	*kf;
	Function	*f;
	bool		is_new;

	ef = ExecutorBC::getFuncByAddr(guest_addr);
	if(ef) 
		return ef;
	f = getFuncByAddrNoKMod(guest_addr, is_new);
	if (f == NULL) return NULL;
	assert (!xxx_debug && "DONE");
//	xxx_debug = (guest_addr == 0x7f360fd8b856);
//	xxx_debug = ((guest_addr & 0xffff) == 0xb960);
	if (!is_new) return f;

	/* stupid kmodule stuff */
	kf = kmodule->addFunction(f);
	statsTracker->addKFunction(kf);
	bindKFuncConstants(kf);
	kmodule->bindModuleConstTable(this);

	return f;
}

static int counter = 0;

void ExecutorVex::executeInstruction(
        ExecutionState &state,
        KInstruction *ki)
{
        Executor::executeInstruction(state, ki);
	if (xxx_debug) {
		ref<Expr>	e;
		counter++;
		if (counter == 1) return;
		e = state.readLocalCell(state.stack.size()-1, ki->dest).value;
		ki->inst->dump(); std::cerr << "=>";
		if (!e.isNull()) e->print(std::cerr);
		else std::cerr << "???";
		std::cerr << "\n";
	}
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
	fprintf(stderr, "before syscall %d(?): states=%d\n",
		sysnr,
		stateManager->size());

	/* arg0 = regctx, arg1 = jmpptr */
	args.push_back(es2esv(state).getRegCtx()->getBaseExpr());
	args.push_back(eval(ki, 0, state).value);

	executeCall(state, ki, kf_scenter->function, args);

	fprintf(stderr, "after syscall: states=%d.\n", stateManager->size());
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
		os << "problem PC:\n";
		state.prevPC->inst->print(os);
		os << "\n";
	}

	top_f = state.stack.back().kf->function;
	os << "Func: ";
	if (top_f)
		top_f->print(os);
	else
		os << "???";
	os << "\n";

	os << "Constraints: \n";
	state.constraints.print(os);
}

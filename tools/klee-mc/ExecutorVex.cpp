#include "llvm/Target/TargetData.h"
#include "klee/Internal/Module/KModule.h"
#include "llvm/System/Path.h"
#include "klee/Config/config.h"
#include "../../lib/Core/TimingSolver.h"
#include "../../lib/Core/StatsTracker.h"
#include "../../lib/Core/ExeStateManager.h"
#include "../../lib/Core/UserSearcher.h"
#include "../../lib/Core/PTree.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <syscall.h>
#include <unistd.h>
#include <vector>

#include "gueststate.h"
#include "guestcpustate.h"
#include "genllvm.h"
#include "vexhelpers.h"
#include "vexxlate.h"
#include "vexsb.h"
#include "vexfcache.h"
#include "syscallparams.h"
#include "static/Sugar.h"

#include "ExecutorVex.h"

extern "C" 
{
#include "valgrind/libvex_guest_amd64.h"
}

using namespace klee;
using namespace llvm;

extern bool WriteTraces;

ExecutorVex::ExecutorVex(
	const InterpreterOptions &opts,
	InterpreterHandler *ih,
	GuestState	*in_gs)
: Executor(opts, ih),
  gs(in_gs),
  exited(false)
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
	if (!theVexHelpers) theVexHelpers = new VexHelpers();

	xlate_cache = new VexFCache(new VexXlate());
	kmodule = new KModule(theGenLLVM->getModule());

	target_data = kmodule->targetData;
	dbgStopPointFn = kmodule->dbgStopPointFn;

	// Initialize the context.
	assert(target_data->isLittleEndian() && "BIGENDIAN??");

	Context::initialize(
		target_data->isLittleEndian(),
		(Expr::Width) target_data->getPointerSizeInBits());

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
	if (kmodule) delete kmodule;
	delete xlate_cache;
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
	state = new ExecutionState(kmodule->getKFunction(init_func));

	/* important to add modules before intializing globals */
	std::list<Module*> l = theVexHelpers->getModules();
	foreach (it, l.begin(), l.end()) {
		kmodule->addModule(*it);
	}

	prepState(state, init_func);
	initializeGlobals(*state);

	processTree = new PTree(state);
	state->ptreeNode = processTree->root;

	std::cerr << "BEGIN INITIAL MAP_-------------------------\n";
	std::cerr << state->addressSpace.objects;
	std::cerr << "END INITIAL MAP-------------------------\n";

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

	fprintf(stderr, "STIL LCLEANING\n");
	if (statsTracker) statsTracker->done();

	fprintf(stderr, "OK.\n");
}


void ExecutorVex::prepState(ExecutionState* state, Function* f)
{
	setupRegisterContext(state, f);
	setupProcessMemory(state, f);
}

#define STACK_EXTEND 0x100000
void ExecutorVex::setupProcessMemory(ExecutionState* state, Function* f)
{
	std::list<GuestMemoryRange*> memmap(gs->getMemoryMap());
	foreach (it, memmap.begin(), memmap.end()) {
		GuestMemoryRange	*gmr;
		MemoryObject		*mmap_mo;
		ObjectState		*mmap_os;
		unsigned int		len;
		unsigned int		copy_offset;
		const char		*data;

		gmr = *it;

		len = gmr->getBytes();
		if (gmr->isStack()) {
			mmap_mo = memory->allocateFixed(
				((uint64_t)gmr->getGuestAddr())-STACK_EXTEND,
				len+STACK_EXTEND,
				f->begin()->begin(),
				state);
			copy_offset = STACK_EXTEND;
			mmap_mo->setName("stack");
		} else {
			mmap_mo = memory->allocateFixed(
				((uint64_t)gmr->getGuestAddr()),
				len,
				f->begin()->begin(),
				state);
			copy_offset = 0;
		}

		data = (const char*)gmr->getData();
		mmap_os = bindObjectInState(*state, mmap_mo, false);
		for (unsigned int i = 0; i < len; i++) {
			state->write8(mmap_os, i+copy_offset, data[i]);
		}
		delete gmr;
	}
}

void ExecutorVex::setupRegisterContext(ExecutionState* state, Function* f)
{
	ObjectState 			*state_regctx_os;
	KFunction			*kf;
	std::vector<ref<Expr> >		args;
	unsigned int			state_regctx_sz;
	
	
	state_regctx_sz = gs->getCPUState()->getStateSize();
	state_regctx_mo = memory->allocate(
		state_regctx_sz,
		false, true,
		f->begin()->begin(), state);
	state_regctx_mo->setName("regctx");
	args.push_back(state_regctx_mo->getBaseExpr());

	if (symPathWriter) state->symPathOS = symPathWriter->open();
	if (statsTracker) statsTracker->framePushed(*state, 0);

	assert(args.size() == f->arg_size() && "wrong number of arguments");

	kf = kmodule->getKFunction(f);
	for (unsigned i = 0, e = f->arg_size(); i != e; ++i)
		bindArgument(kf, i, *state, args[i]);

	if (!state_regctx_mo) return;
	state_regctx_os = bindObjectInState(*state, state_regctx_mo, false);

	const char*  state_data = (const char*)gs->getCPUState()->getStateData();
	for (unsigned int i=0; i < state_regctx_sz; i++)
		state->write8(state_regctx_os, i, state_data[i]);
}

void ExecutorVex::run(ExecutionState &initialState)
{
	bindModuleConstants();
	Executor::run(initialState);
}

void ExecutorVex::bindModuleConstants(void)
{
	foreach (it, kmodule->kfuncsBegin(), kmodule->kfuncsEnd()) {
		bindKFuncConstants(*it);
	}

	bindModuleConstTable();
}

void ExecutorVex::bindModuleConstTable(void)
{
	if (kmodule->constantTable) delete [] kmodule->constantTable;

	kmodule->constantTable = new Cell[kmodule->constants.size()];
	for (unsigned i = 0; i < kmodule->constants.size(); ++i) {
		Cell &c = kmodule->constantTable[i];
		c.value = evalConstant(kmodule->constants[i]);
	}
}

void ExecutorVex::bindKFuncConstants(KFunction* kf)
{
	for (unsigned i=0; i<kf->numInstructions; ++i)
		bindInstructionConstants(kf->instructions[i]);
}

Function* ExecutorVex::getFuncFromAddr(uint64_t guest_addr)
{
	uint64_t	host_addr;
	KFunction	*kf;
	Function	*f;
	VexSB		*vsb;

	host_addr = gs->addr2Host(guest_addr);
	if (!host_addr) host_addr = guest_addr;

	/* cached => already seen it */
	f = xlate_cache->getCachedFunc(guest_addr);
	if (f != NULL) return f;

	/* !cached => put in cache, alert kmodule, other bookkepping */
	f = xlate_cache->getFunc((void*)host_addr, guest_addr);
	if (f == NULL) return NULL;

	/* need to know func -> vsb to compute func's guest address */
	vsb = xlate_cache->getCachedVSB(guest_addr);
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
//	fprintf(stderr, "vex::exeInst: "); ki->inst->dump();
	Executor::executeInstruction(state, ki);
}

void ExecutorVex::dumpRegs(ExecutionState& state)
{
	updateGuestRegs(state);
	gs->getCPUState()->print(std::cerr);
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
	if (vsb == NULL) {
		/* no VSB => outcall; use default KLEE return handling */
		assert (state.stack.size() > 1);
		Executor::retFromNested(state, ki);
		return;
	}
	assert (vsb && "Could not translate current function to VSB");


	if (vsb->isCall()) {
		handleXferCall(state, ki);
		return;
	} else if (vsb->isSyscall()) {
		if (!handleXferSyscall(state, ki)) {
			terminateStateOnExit(state);
		}
		return;
	} else if (vsb->isReturn()) {
		handleXferReturn(state, ki);
		return;
	} else {
		handleXferJmp(state, ki);
		return;
	}

	/* XXX need better bad stack frame handling */
	ref<Expr> result = ConstantExpr::alloc(0, Expr::Bool);
	result = eval(ki, 0, state).value;

	fprintf(stderr, "terminating initial stack frame\n");
	fprintf(stderr, "result: ");
	result->dump();
	terminateStateOnExit(state);
	return;
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
	ref<ConstantExpr>	value;
	uint64_t		addr;
	bool			success;

	if (iter.free == NULL) return false;

	success = solver->getValue(*(iter.free), iter.v, value);
	assert(success && "FIXME: Unhandled solver failure");
	(void) success;

	iter.res = fork(*(iter.free), EqExpr::create(iter.v, value), true);
	iter.first = false;
	iter.free = iter.res.second;

	if (!iter.res.first) return xferIterNext(iter);

	addr = value->getZExtValue();
	if (addr == 0 || addr > 0x7fffffffffffULL) {
		terminateStateOnError(
			*(iter.res.first),
			"fork error: jumping to bad pointer",
			"badjmp.err");
		return xferIterNext(iter);
	}

	iter.f = getFuncFromAddr(addr);
	assert (iter.f != NULL && "BAD FUNCTION TO JUMP TO");

	// Don't give warning on unique resolution
	if (iter.res.second || !iter.first) {
	// don't warn here since symbolic jumps are so frequent in MC.. 
	#if 0
		klee_warning_once(
			(void*) (unsigned long) addr,
			"resolved symbolic function pointer to: %s",
			iter.f->getName().data());
	#endif
	}

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
	KInstIterator	ki = state.getCaller();

	assert (kf != NULL);
	assert (state.stack.size() > 0);
	/* save, pop off old state */
	cpn = state.stack.back().callPathNode;
	state.popFrame();

	/* create new frame to replace old frame;
	   new frame initialized with target function kf */
	state.pushFrame(ki, kf);
	StackFrame	&sf = state.stack.back();
	sf.callPathNode = cpn;

	/* set new state */
	state.pc = kf->instructions;
	bindArgument(kf, 0, state, state_regctx_mo->getBaseExpr());
}

/* xfers are done with an address in the return value of the next place to
 * jump.  g=f(x) => g(x) -> f(x). (g directly follows f) */
void ExecutorVex::handleXferCall(ExecutionState& state, KInstruction* ki)
{
	std::vector< ref<Expr> > 	args;
	struct XferStateIter		iter;

	args.push_back(state_regctx_mo->getBaseExpr());
	xferIterInit(iter, &state, ki);
	while (xferIterNext(iter)) {
		executeCall(*(iter.res.first), ki, iter.f, args);
	}
}

void ExecutorVex::sc_writev(ExecutionState& state)
{
	/* doesn't affect proces state! AIEE */
	sc_ret_ge0(state);
	return;
}

ObjectState* ExecutorVex::makeSCRegsSymbolic(ExecutionState& state)
{
	ObjectState		*state_regctx_os;
	const ObjectState	*old_regctx_os;
	unsigned int		sz;

	sz = gs->getCPUState()->getStateSize();

	/* hook into guest state with state_regctx_mo,
	 * use mo to mark memory as symbolic */
	old_regctx_os = state.addressSpace.findObject(state_regctx_mo);

	/* 1. make all of symbolic */
	state.constraints.removeConstraintsPrefix("reg");
	state_regctx_os = executeMakeSymbolic(state, state_regctx_mo, "reg");

	/* 2. set everything that should be initialized */
	const char*  state_data = (const char*)gs->getCPUState()->getStateData();
	for (unsigned int i=0; i < sz; i++) {
		unsigned int	reg_idx;

		reg_idx = i/8;
		if (	reg_idx == offsetof(VexGuestAMD64State, guest_RAX)/8 ||
			reg_idx == offsetof(VexGuestAMD64State, guest_RCX)/8 ||
			reg_idx == offsetof(VexGuestAMD64State, guest_R11)/8)
		{
			/* ignore rax, rcx, r11 */
			continue;
		}

		state.write8(state_regctx_os, i, state_data[i]);
	}

	return state_regctx_os;
}

ObjectState* ExecutorVex::sc_ret_ge0(ExecutionState& state)
{
	ObjectState		*state_regctx_os;
	bool			constrained;
	ref<Expr>		success_constraint;

	state_regctx_os = makeSCRegsSymbolic(state);

	/* 3. force zero <= sysret; sysret >= 0; no errors */
	success_constraint = SleExpr::create(
		ConstantExpr::create(0, 64),
		state.read(
			state_regctx_os,
			offsetof(VexGuestAMD64State, guest_RAX),
			64));

	constrained = addConstraint(state, success_constraint);
	assert (constrained); // symbolic, should always succeed..
	
	return state_regctx_os;
}


ObjectState* ExecutorVex::sc_ret_le0(ExecutionState& state)
{
	ObjectState		*state_regctx_os;
	bool			constrained;
	ref<Expr>		success_constraint;

	state_regctx_os = makeSCRegsSymbolic(state);

	/* 3. force zero >= sysret;  sysret <= zero; sometimes errors */
	success_constraint = SgeExpr::create(
		ConstantExpr::create(0, 64),
		state.read(
			state_regctx_os,
			offsetof(VexGuestAMD64State, guest_RAX),
			64));

	constrained = addConstraint(state, success_constraint);
	assert (constrained); // symbolic, should always succeed..
	
	return state_regctx_os;
}


ObjectState* ExecutorVex::sc_ret_range(
	ExecutionState& state,
	uint64_t lo, uint64_t hi)
{
	ObjectState		*state_regctx_os;
	bool			constrained;
	ref<Expr>		success_constraint;

	state_regctx_os = makeSCRegsSymbolic(state);

	success_constraint = SleExpr::create(
		ConstantExpr::create(lo, 64),
		state.read(
			state_regctx_os,
			offsetof(VexGuestAMD64State, guest_RAX),
			64));

	constrained = addConstraint(state, success_constraint);
	assert (constrained); // symbolic, should always succeed..

	success_constraint = SgeExpr::create(
		ConstantExpr::create(hi, 64),
		state.read(
			state_regctx_os,
			offsetof(VexGuestAMD64State, guest_RAX),
			64));

	constrained = addConstraint(state, success_constraint);
	assert (constrained); // symbolic, should always succeed..

	return state_regctx_os;
}



void ExecutorVex::sc_munmap(ExecutionState& state)
{
	SyscallParams		sp(gs->getSyscallParams());
	uint64_t		addr;
	uint64_t		len;
	const MemoryObject	*mo;

	addr = sp.getArg(0);
	len = sp.getArg(1);
	mo = state.addressSpace.resolveOneMO(addr);
	assert (mo);
	fprintf(stderr, "FREEING %p-%p, mo=%p-%p\n",
		addr, addr+len, mo->address, mo->address+mo->size);
	assert (mo->size == len && mo->address == addr);
	state.addressSpace.unbindObject(mo);
	sc_ret_le0(state);
}

void ExecutorVex::sc_fail(ExecutionState& state)
{
	sc_ret_v(state, -1);
}

void ExecutorVex::sc_stat(ExecutionState& state)
{
	SyscallParams		sp(gs->getSyscallParams());
	ObjectState		*os_statbuf;
	uint64_t		statbuf_addr;
	const MemoryObject	*mo_statbuf;

	statbuf_addr = sp.getArg(1);
	mo_statbuf = state.addressSpace.resolveOneMO(statbuf_addr);
	
	/* set stat structure as symbolic */
	os_statbuf = executeMakeSymbolic(
		state,
		mo_statbuf,
		ConstantExpr::alloc(sizeof(struct stat), 32),
		"statbuf");

	/* fail or success */
	sc_ret_le0(state);
}

void ExecutorVex::updateGuestRegs(ExecutionState& state)
{
	void	*guest_regs;
	guest_regs = gs->getCPUState()->getStateData();
	state.addressSpace.copyToBuf(state_regctx_mo, guest_regs);
}


void ExecutorVex::sc_read(ExecutionState& state)
{
	SyscallParams		sp(gs->getSyscallParams());
	uint64_t		buf_sz, buf_addr;
	ObjectState		*os;
	const MemoryObject	*mo;

	buf_addr = sp.getArg(1);
	buf_sz = sp.getArg(2);
	mo = state.addressSpace.resolveOneMO(buf_addr);

	os = executeMakeSymbolic(
		state,
		mo,
		ConstantExpr::alloc(buf_sz, 32),
		"readbuf");

	sc_ret_v(state, buf_sz);
}

void ExecutorVex::sc_getcwd(ExecutionState& state)
{
	SyscallParams		sp(gs->getSyscallParams());
	uint64_t		buf_sz, buf_addr;
	ObjectState		*os_cwdbuf;
	const MemoryObject	*mo_cwdbuf;

	buf_addr = sp.getArg(0);
	buf_sz = sp.getArg(1);
	mo_cwdbuf = state.addressSpace.resolveOneMO(buf_addr);
	
	/* return symbolic path name */
	os_cwdbuf = executeMakeSymbolic(
		state,
		mo_cwdbuf,
		ConstantExpr::alloc(buf_sz, 32),
		"cwdbuf");

	/* ensure buffer is null-terminated */
	state.write8(
		os_cwdbuf, 
		(buf_addr - mo_cwdbuf->address) + (buf_sz-1),
		0);

	/* TODO: simulate errors */
	sc_ret_v(state, buf_addr);
}

void ExecutorVex::sc_mmap(ExecutionState& state, KInstruction* ki)
{
	SyscallParams		sp(gs->getSyscallParams());
	MemoryObject		*new_mo;
	ObjectState		*new_os;
	uint64_t		addr = sp.getArg(0);
	uint64_t		length = sp.getArg(1);
	uint64_t		prot = sp.getArg(2);
#if 0
/* Will be useful sometime? */
	uint64_t		flags = sp.getArg(3);
	uint64_t		fd = sp.getArg(4);
	uint64_t		offset = sp.getArg(5);
#endif

	if (addr == 0) {
		/* not requesting an address */
		new_mo = memory->allocate(
			length, 
			true /* local */,
			false /* global */, 
			ki->inst,
			&state);
		if (new_mo) addr = new_mo->address;
	} else {
		/* requesting an address */
		new_mo = addExternalObject(
			state, (void*)addr, length,
			((prot & PROT_WRITE) == 0) /* isReadOnly */);
	}

	if (new_mo == NULL) {
		/* Couldn't create memory object, 
		 * most likely because size was too big */
		sc_fail(state);
		return;
	}

	/* returned data will be symbolic */
	new_mo->setName("mmaped");
	new_os = executeMakeSymbolic(state, new_mo, "mmap");
	assert (new_os != NULL && "Could not make object state");
	if ((prot & PROT_WRITE) == 0)
		new_os->setReadOnly(true);

	/* always succeed */
	/* TODO: should we enable failures here? */
	sc_ret_v(state, addr);

	fprintf(stderr, "MMAP ADDR=%p-%p\n", addr, addr + length);
}

void ExecutorVex::sc_ret_v(ExecutionState& state, uint64_t v)
{
	ObjectState* state_regctx_os;
	state_regctx_os = state.addressSpace.findObject(state_regctx_mo);
	state.write64(
		state_regctx_os, 
		offsetof(VexGuestAMD64State, guest_RAX),
		v);
}

/* system calls serve three purposes: 
 * 1. xfer data
 * 2. manage state
 * 3. launch processes
 *
 * For 1. klee can cheat 100% and just make things symbolic.
 * For 2. klee can get away with acting dumb (just emulate mmap)
 * For 3. bonetown, but we can't handle threads yet anyhow
 */
bool ExecutorVex::handleXferSyscall(
	ExecutionState& state, KInstruction* ki)
{
	bool	ret;

	updateGuestRegs(state);
	SyscallParams   sp(gs->getSyscallParams());
	int		sys_nr;

	sys_nr = sp.getSyscall();

	fprintf(stderr, "SYSCALL %d! sm=%d\n", 
		sys_nr,
		stateManager->size());

	sc_dispatched++;
	switch(sys_nr) {
	case SYS_sched_setaffinity:
	case SYS_sched_getaffinity:
		sc_fail(state);
		break;
	case SYS_fadvise64:
		sc_ret_v(state, 0);
		break;
	case SYS_read:
		if (rand() % 2) 
			sc_fail(state);
		else 
			sc_read(state);
		break;
	case SYS_open:
		/* what kind of checks do we care about here? */
		sc_ret_ge0(state);
		break;
	case SYS_write:
		sc_ret_ge0(state);
		break;
	case SYS_writev: {
		// fd, iov, iovcnt
		sc_ret_ge0(state);
		break;
	}
	case SYS_fstat:
	case SYS_stat:
		sc_stat(state);
		break;
	case SYS_getcwd:
		sc_getcwd(state);
		break;
	case SYS_brk:
		// always fail this, just like in pt_run
		sc_fail(state);
		break;
	case SYS_ioctl:
		sc_ret_ge0(state);
		break;
	case SYS_mremap:
		sc_fail(state);
		break;
	case SYS_munmap:
		sc_munmap(state);
		break;
	case SYS_mmap:
		sc_mmap(state, ki);
		break;
	case SYS_exit:
	case SYS_exit_group:
		fprintf(stderr, "EXITING ON sys_nr=%d\n", sys_nr);
		ret = false;
		goto done;
	case SYS_rt_sigaction:
		/* TODO: fake sigaction struct? */
		sc_ret_v(state, 0);
		break;
	default:
		fprintf(stderr, "UNKNOWN SYSCALL 0x%x\n", sys_nr);
		gs->print(std::cerr);
		assert (0 == 1);
		break;
	}

	handleXferJmp(state, ki);
	ret = true;
done:
	sc_retired++;
	return ret;
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

void ExecutorVex::initializeGlobals(ExecutionState &state)
{
	Module *m;

	m = kmodule->module;
	fprintf(stderr, "INIT GLOBALS!!!!!!!!!!!!!!!!!!!\n");

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
assert(0 == 1);
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

	os = bindObjectInState(state, mo, false);
	globalObjects.insert(std::make_pair(&gv, mo));
	globalAddresses.insert(std::make_pair(&gv, mo->getBaseExpr()));

	if (!gv.hasInitializer()) os->initializeToRandom();
}

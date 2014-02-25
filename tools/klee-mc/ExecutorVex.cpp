#include <llvm/IR/DataLayout.h>
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
#include "klee/Internal/Support/Timer.h"

#include <stdio.h>
#include <vector>

#include "HostAccelerator.h"
#include "KModuleVex.h"
#include "symbols.h"
#include "guestcpustate.h"
#include "guestsnapshot.h"
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

#include "cpu/amd64cpustate.h"
extern "C"
{
#include "valgrind/libvex_guest_amd64.h"
#include "valgrind/libvex_guest_x86.h"
#include "valgrind/libvex_guest_arm.h"
}

using namespace klee;
using namespace llvm;

extern bool WriteTraces;

extern bool SymArgs, SymArgC;
extern bool UsePrioritySearcher;

bool SymRegs = false;
bool UseConcreteVFS;

#define GET_SFH(x)	static_cast<SyscallSFH*>(x)

namespace
{
	cl::opt<bool> ShowSyscalls("show-syscalls");
	cl::opt<bool> SymMagic(
		"sym-magic", cl::desc("Mark 'magic' 0xa3 bytes as symbolic"));

	cl::opt<bool> AllowZeroArgc(
		"allow-zero-argc",
		cl::desc("Permit argc to be equal to zero"));

	cl::opt<bool> HWAccel(
		"use-hwaccel",
		cl::desc("Use hardware acceleration on concrete state."));

	cl::opt<bool> XChkHWAccel(
		"xchk-hwaccel",
		cl::desc("Cross-check hw accel with interpreter."));

	cl::opt<bool> KeepDeadStack(
		"keep-dead-stack",
		cl::desc("Keep registers in dead stack frames"));

	cl::opt<bool,true> SymRegsProxy(
		"symregs",
		cl::desc("Mark initial register file as symbolic"),
		cl::location(SymRegs));

	cl::opt<bool> LogRegs("logregs", cl::desc("Log registers."));
	cl::opt<bool> LogStack("logstack", cl::desc("Log stack."));
	cl::opt<unsigned long long> LogObject("logobj", cl::desc("Log memory object."));

	cl::opt<bool,true> ConcreteVfsProxy(
		"concrete-vfs",
		cl::desc("Treat absolute path opens as concrete"),
		cl::location(UseConcreteVFS));

	cl::opt<bool> UseFDT("use-fdt", cl::desc("Use TJ's FDT model"));
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

	cl::opt<bool> UseRegPriority("use-reg-pr",
		cl::desc("Priority by reg file"));

	cl::opt<std::string> RunSym("run-func", cl::desc("Function to run."));
}


void ExecutorVex::setKeepDeadStack(bool v)
{
	std::cerr << "[kleevex] Overriding KeepDeadStack = " << v << '\n';
	KeepDeadStack = v;
}

ExecutorVex::ExecutorVex(InterpreterHandler *ih)
: Executor(ih)
, gs(dynamic_cast<KleeHandlerVex*>(ih)->getGuest())
, sys_model(0)
, img_init_func(0)
, img_init_func_addr(0)
{
	GuestSnapshot*	ss = dynamic_cast<GuestSnapshot*>(gs);
	assert (kmodule == NULL && "KMod already initialized? My contract!");

	ExeStateBuilder::replaceBuilder(new ExeStateVexBuilder());
	ExeStateVex::setBaseGuest(gs);
	ExeStateVex::setBaseStack(gs->getCPUState()->getStackPtr().o);

	if (gs->getMem()->is32Bit()) MemoryManager::set32Bit();

	if (UsePrioritySearcher) {
		Prioritizer	*pr = NULL;

		if (UseSyscallPriority) pr = new SyscallPrioritizer();
		else if (UseRegPriority) pr = new RegPrioritizer(*this);

		UserSearcher::setPrioritizer(pr);
	}

	/* XXX TODO: module flags */
	ModuleOptions mod_opts(false, false, std::vector<std::string>());

	assert (gs);

	if (!theGenLLVM) theGenLLVM = new GenLLVM(gs);
	if (!theVexHelpers) theVexHelpers = VexHelpers::create(gs->getArch());

	std::cerr << "[klee-mc] Forcing fake vsyspage reads\n";
	theGenLLVM->setFakeSysReads();

	if (ss != NULL && ss->getPlatform("pbi") == true) {
		if (ss->getPlatform("version"))
			sys_model = W32Model::createWin7(this);
		else
			sys_model = W32Model::createXP(this);
	}

	if (sys_model == NULL) {
		if (UseFDT)		sys_model = new FDTModel(this);
		else if (UseSysNone)	sys_model = new NoneModel(this);
		else			sys_model = new LinuxModel(this);
	}
	theVexHelpers->loadUserMod(sys_model->getModelFileName());

	assert (kmodule == NULL);

	/* occasionally want to load something elsewhere
	 * (ARM 0x8000 code base,
	 *  0xf..fe000 faketimer page on fogger, ...)
	 * problematic because the codegen normally updates
	 * loads/stores to reflect these new addresses.
	 * unsetting the bias should revert to unbiased behavior */
	if (getenv("VEXLLVM_BASE_BIAS") != NULL) {
		unsetenv("VEXLLVM_BASE_BIAS");
		theGenLLVM->setUseReloc(false);
	}

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

	statsTracker = StatsTracker::create(
		*this,
		kmodule,
		interpreterHandler->getOutputFilename("assembly.ll"),
		mod_opts.ExcludeCovFiles);

	hw_accel = (HWAccel && gs->getArch() == Arch::X86_64)
		? HostAccelerator::create()
		: NULL;
}

ExecutorVex::~ExecutorVex(void)
{
	if (hw_accel) delete hw_accel;
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

	srand(time(0));
	srandom(time(0));

	// acrobatics because there's an annoying circular dependency
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
	kf_scenter->isSpecial = true;

	if (SymArgs) makeArgsSymbolic(state);
	if (SymArgC) makeArgCSymbolic(state);
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
	statsTracker->done();
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

void ExecutorVex::makeArgCSymbolic(ExecutionState* state)
{
	ObjectPair		op;
	unsigned		argc_max;
	Expr::Width		bits;
	guest_ptr		argc_ptr(gs->getArgcPtr());
	ref<Expr>		constr;

	if (!argc_ptr) {
		std::cerr << "[klee-mc] Couldn't reconstrain argc.\n";
		return;
	}

	bits = gs->getMem()->is32Bit() ? 32 : 64;

	std::cerr << "[klee-mc] Set argc symbolic\n";
	GET_SFH(sfh)->makeRangeSymbolic(
		*state, gs->getMem()->getHostPtr(argc_ptr), bits / 8, "argc");
	argc_max = gs->getMem()->readNative(argc_ptr);

	state->addressSpace.resolveOne(argc_ptr.o, op);
	assert (op_mo(op) != NULL);
	assert (op_mo(op)->address == argc_ptr.o);

	constr = MK_ULE(
		state->read(op_os(op), 0, bits), MK_CONST(argc_max, bits));
	state->addConstraint(constr);

	if (!AllowZeroArgc) {
		constr = MK_UGT(
			state->read(op_os(op), 0, bits), MK_CONST(0, bits));
		state->addConstraint(constr);
	}
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
	unsigned int pgnum,
	Guest* g)
{
	const char		*data;
	MemoryObject		*mmap_mo;
	const ObjectState	*mmap_os_c;
	ObjectState		*mmap_os;
	uint64_t		addr_base;
	void			*buf_base;
	uint64_t		heap_min, heap_max;

	assert (m.getBytes() > pgnum*PAGE_SIZE);
	assert ((m.getBytes() % PAGE_SIZE) == 0);
	assert ((m.offset.o & (PAGE_SIZE-1)) == 0);

	if (g == NULL) g = gs;

	buf_base = (void*)(
		((uint64_t)g->getMem()->getData(m))+(PAGE_SIZE*pgnum));
	addr_base = m.offset.o + PAGE_SIZE*pgnum;

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
	} else {
		mmap_mo->setName(
			(m.getName().empty() == true)
				? "guestimg"
				: m.getName().c_str());
	}

	/* optimize for zero pages */
	data = (const char*)buf_base;
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
		mmap_os->resetCopyDepth();
		break;
	}

	for (; i < PAGE_SIZE; i++) {
		/* bug fiend note:
		 * valgrind complains line because of data[i] on the syspage.
		 * Linux keeps a syscall page at 0xf..f600000 (vsyscall),
		 * but valgrind doesn't know this because it traps vsyscalls.
		 * Safe, but will need a workaround *eventually* */
		state->write8(mmap_os, i, data[i]);
	}

	if (heap_min != ~0UL && heap_max != 0) {
		/* scanning memory is kind of stupid, but we're desperate */
		sys_model->setModelU64(kmodule->module, "heap_begin", heap_min);
		 /* max = start of last page */
		sys_model->setModelU64(
			kmodule->module, "heap_end", heap_max + 4096);

		SFH_ADD_REG("heap_begin", heap_min);
		SFH_ADD_REG("heap_end",  heap_max + 4096);
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

	if (m.type == GuestMem::Mapping::STACK) {
		SFH_ADD_REG("stack_begin", m.offset.o);
		SFH_ADD_REG("stack_end", m.offset.o + m.length);
	}
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
	const char	*reg_data;

	state_regctx_mo = allocRegCtx(state, f);
	es2esv(*state).setRegCtx(state_regctx_mo);

	if (SymRegs) {
		std::cerr << "[ExeVex] Making register symbolic.\n";
		state_regctx_os = makeSymbolic(*state, state_regctx_mo, "reg");
	} else
		state_regctx_os = state->bindMemObjWriteable(state_regctx_mo);

	if (symPathWriter) state->symPathOS = symPathWriter->open();
	statsTracker->framePushed(*state, 0);

	kf = kmodule->getKFunction(f);
	assert (f->arg_size() == 1);
	state->bindArgument(kf, 0, state_regctx_mo->getBaseExpr());

	reg_data = (const char*)gs->getCPUState()->getStateData();

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
					reg_data[off+i]);
		}

		markExit(*state, GE_IGNORE);
		return;
	}

	state_regctx_sz = gs->getCPUState()->getStateSize();
	for (unsigned int i = 0; i < state_regctx_sz; i++)
		state->write8(state_regctx_os, i, reg_data[i]);

	state_regctx_os->resetCopyDepth();
}

void ExecutorVex::run(ExecutionState &initialState)
{
	km_vex->bindModuleConstants(this);
	Executor::run(initialState);
}

/* need to hand roll our own instRet because we want to be able to
 * jump between super blocks based on ret values */
void ExecutorVex::instRet(ExecutionState &state, KInstruction *ki)
{
	KFunction	*kf(state.stack.back().kf);

	/* need to trapeze between VSB's; depending on exit type,
	 * translate VSB exits into LLVM instructions on the fly */
	if (!kf->isSpecial) {
		/* no VSB => outcall to externa LLVM bitcode;
		 * use default KLEE return handling */
		assert (state.stack.size() > 1);
		Executor::retFromNested(state, ki);
		return;
	}

	if (kf->function == kf_scenter->function) {
		/* If leaving the sc_enter function, need to know to pop the stack.
		 * Otherwies, the exit will look like a jump
		 * and keep stale entries on the callstack */
		markExit(state, GE_RETURN);

		es2esv(state).setLastSyscallInst();

		/* hardware acceleration begins at system call exit */
		if (hw_accel != NULL && !state.getOnFini()) {
			if (!doAccel(state, ki))
				return;
		}
	}

	handleXfer(state, ki);
}

bool ExecutorVex::doAccel(ExecutionState& state, KInstruction* ki)
{
	HostAccelerator::Status	s;
	bool			ret(true);
	ExecutionState		*shadow_es(NULL);
	ref<ConstantExpr>	new_pc_e;
	int			vnum;
	uint64_t		x_reg;
	WallTimer		wt;
	double			t_accel(0), t_xchk;

	vnum = ki->getOperand(0);
	if (vnum < 0) goto done;

	if (XChkHWAccel) shadow_es = pureFork(state);

	/* compute PC from return value (hack hack hack) */
	new_pc_e = dyn_cast<ConstantExpr>(eval(ki, 0, state));
	es2esv(state).setAddrPC(new_pc_e->getZExtValue());
	s = hw_accel->run(es2esv(state));

	/* couldn't host accel? skip */
	if (	s != HostAccelerator::HA_PARTIAL &&
		s != HostAccelerator::HA_SYSCALL) goto done;

	/* rebind return address */
	new_pc_e = MK_CONST(es2esv(state).getAddrPC(), 64);
	es2esv(state).setAddrPC(new_pc_e->getZExtValue());
	state.stack.getTopCell(vnum).value = new_pc_e;

	if (shadow_es == NULL) return true;
	if (s != HostAccelerator::HA_SYSCALL)
		goto done;

	t_accel = wt.checkSecs();
	wt.reset();

	/* now run with shadow state up to syscall */
	std::cerr << "[HWAccelXChk] Begin interpreter retrace\n";
	handleXfer(*shadow_es, ki);
	if (!runToFunction(shadow_es, kf_scenter)) {
		ret = false;
		goto done;
	}

	/* pull in destination RIP */
	new_pc_e = dyn_cast<ConstantExpr>(eval(shadow_es->prevPC, 0, *shadow_es));
	x_reg = new_pc_e->getZExtValue();
	/* shadow state is left on a GE_SYSCALL, hw accel expects GE_RETURN */
	markExit(*shadow_es, GE_RETURN);

	AS_COPYOUT(*shadow_es, &x_reg, VexGuestAMD64State, guest_RIP, 8);
	ret = hw_accel->xchk(state, *shadow_es);
done:
	t_xchk = wt.checkSecs();

	if (ret == false) {
		/* shadow state can carry on! */
		TERMINATE_ERROR(this, state, "Bad hwaccel xchk", "hwxchk.err");
	} else if (shadow_es != NULL)
		terminate(*shadow_es);

	if (t_accel > 0.0) std::cerr <<
		"[hwaccel] hw=" << t_accel << ". klee=" << t_xchk << '\n';

	return ret;
}

void ExecutorVex::markExit(ExecutionState& es, uint8_t v)
{
	gs->getCPUState()->setExitType(GE_IGNORE);
	es.write8(GETREGOBJ(es), gs->getCPUState()->getExitTypeOffset(), v);
}

// KLEE MIPS = 2.7e6 * 0.1 = T_BASE
#define HW_ACCEL_WATERMARK	400000
/* handle transfering between VSB's */
void ExecutorVex::handleXfer(ExecutionState& state, KInstruction *ki)
{
	GuestExitType	exit_type;
	KFunction	*onRet;
	ExeStateVex	*esv;

	esv = &es2esv(state);

	exit_type = (GuestExitType)getExitType(state);

	/* onRet support-- call hooked function on stack pop */
	if (	exit_type == GE_RETURN &&
		(onRet = state.stack.back().onRet) != NULL)
	{
		std::vector<ref<Expr> >	args;

		state.stack.back().onRet = NULL;
		state.stack.back().stackWatermark = state.getStackDepth();
		state.abortInstruction();

		args.push_back(state.stack.back().onRet_expr);
		executeCall(state, state.pc, onRet->function, args);
		return;
	}

#if 0
	if (hw_accel)
	if (esv->getInstSinceSyscall() > HW_ACCEL_WATERMARK)
	if (ki->getOperand(0) >= 0) {
		std::cerr << "TIME TO ACCELERATE!! totalInsts=" <<
			esv->totalInsts << " vs " <<
			esv->getInstSinceSyscall() << "\n";

		esv->setLastSyscallInst();
		if (!doAccel(state, ki))
			return;
	}
#endif

	if (LogRegs) esv->logXferRegisters();
	if (LogStack) esv->logXferStack();
	if (LogObject) esv->logXferMO(LogObject);

	markExit(state, GE_IGNORE);

	switch(exit_type) {
	case GE_CALL: handleXferCall(state, ki); break;;
	case GE_RETURN: handleXferReturn(state, ki); break;
	case GE_INT:
	case GE_SYSCALL:
		/* it's important to retain the exact exit type for windows
		 * so the model can distinguish between system call types */
		markExit(state, exit_type);
		handleXferSyscall(state, ki);
		return;
	case GE_EMWARN:
		std::cerr << "[VEXLLVM] VEX Emulation warning!?\n";
		handleXferJmp(state, ki);
		break;
	case GE_YIELD: {
		static int		yield_c = 0;
		static ExecutionState	*last_es = NULL;

		if (last_es != &state) yield_c = 0;
		yield_c++;

		if (yield_c > 50) {
			std::cerr << "[VEXLLVM] Killing Vex Yield Loop\n";
			TERMINATE_ERROR(this,
				state,
				"VEX Yied Loop: too many yield calls for state",
				"vexyield.err");
		}
		break;
	}
	case GE_IGNORE: handleXferJmp(state, ki); break;
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
	default: {
		std::cerr << "WTF: EXIT_TYPE=" << exit_type << '\n';
		/* XXX need better bad stack frame handling */
		ref<Expr> result = MK_CONST(0, Expr::Bool);
		result = eval(ki, 0, state);

		std::cerr <<  "terminating initial stack frame\nresult: ";
		result->dump();
		TERMINATE_EXIT(this, state);
		assert (0 == 1 && "SPECIAL EXIT TYPE");
		return; }
	}
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

	if (DumpSyscallStates) {
		static int	n = 0;
		char		prefix[32];
		Query		q(state.constraints, MK_CONST(1,1));

		n++;
		sprintf(prefix, "sc-%d.%d", es2esv(state).getSyscallCount(), n);
		SMTPrinter::dump(q, prefix);
	}

	es2esv(state).incSyscallCount();

	if (ShowSyscalls) {
		uint64_t sysnr = 0;
		switch (gs->getArch()) {
		case Arch::X86_64: AS_COPY(VexGuestAMD64State, guest_RAX, 8); break;
		case Arch::ARM: AS_COPY(VexGuestARMState, guest_R7, 4); break;
		case Arch::I386: AS_COPY(VexGuestX86State, guest_EAX, 4); break;
		default: assert (0 == 1 && "ULP");
		}

		std::cerr << "[klee-mc] before syscall "
			<< sysnr
			<< "(?): states=" << stateManager->size()
			<< ". objs=" << state.getNumSymbolics()
			<< ". st=" << (void*)&state
			<< ". n=" << es2esv(state).getSyscallCount() << '\n';
	}

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
		ExecutionState	*new_state;

		new_state = iter.res.first;
		if (stack_depth > 1) {
			/* pop frame to represent a 'return' */
			/* if the depth < 1, treat a ret like a jump */
			new_state->popFrame();
		}

		jumpToKFunc(*new_state, kmodule->getKFunction(iter.f));
	}
}

/* llvm code has a 'call' which resolves to code can't link in directly. */
void ExecutorVex::callExternalFunction(
	ExecutionState &state,
	KInstruction *target,
	llvm::Function *function,
	std::vector< ref<Expr> > &arguments)
{
	// check if specialFunctionHandler wants it
	if (sfh->handle(state, function, target, arguments))
		return;

	std::cerr << "KLEE: ERROR: Calling non-special external function : "
		<< function->getName().str() << "\n";
	TERMINATE_ERROR(this, state, "externals disallowed", "user.err");
}

void ExecutorVex::printStackTrace(
	const ExecutionState& st,
	std::ostream& os) const
{
	unsigned idx = 0;
	foreach (it, st.stack.rbegin(), st.stack.rend()) {
		const StackFrame	&sf(*it);
		Function		*f(sf.kf ? sf.kf->function : NULL);
		const VexSB		*vsb(f ? km_vex->getVSB(f) : NULL);

		os	<< "\t#" << idx++ << " in "
			<< (f ? f->getName().str() : "(nil)");
		if (vsb != NULL)
			os << " (" << gs->getName(vsb->getGuestAddr()) << ')';

//		os << ' ' << sf.stackWatermark;
//		if (sf.onRet) os << '*';

		os << "\n";
	}
}

#define READ_V(s, x, y)	(s).read(x, y, gs->getMem()->is32Bit() ? 32 : 64)

ref<Expr> ExecutorVex::getCallArg(ExecutionState& es, unsigned int n) const
{ return READ_V(es, GETREGOBJRO(es), gs->getCPUState()->getFuncArgOff(n)); }

ref<Expr> ExecutorVex::getRetArg(ExecutionState& es) const
{ return READ_V(es, GETREGOBJRO(es), gs->getCPUState()->getRetOff()); }

uint64_t ExecutorVex::getStateStack(ExecutionState& s) const
{
	ref<Expr>		stack_e;
	const ConstantExpr	*stack_ce;

	stack_e = READ_V(s,GETREGOBJRO(s),gs->getCPUState()->getStackRegOff());
	stack_ce = dyn_cast<ConstantExpr>(stack_e);
	return (stack_ce != NULL) ? stack_ce->getZExtValue() : 0;
}

llvm::Function* ExecutorVex::getFuncByAddr(uint64_t addr)
{
	if (globals->isLegalFunction(addr))
		return (llvm::Function*)addr;
	return km_vex->getFuncByAddr(addr);
}

unsigned ExecutorVex::getExitType(const ExecutionState& state) const
{
	unsigned	exit_off;
	ref<Expr>	e;

	exit_off = gs->getCPUState()->getStateSize()-1;
	e = state.read8(GETREGOBJRO(state), exit_off);
	assert (e->getKind() == Expr::Constant);

	return (GuestExitType)(cast<ConstantExpr>(e)->getZExtValue());
}

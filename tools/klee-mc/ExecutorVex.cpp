#include "llvm/Target/TargetData.h"
#include "llvm/System/Path.h"
#include "klee/Config/config.h"
#include "../../lib/Core/TimingSolver.h"
#include "../../lib/Core/StatsTracker.h"
#include "../../lib/Core/ExeStateManager.h"
#include "../../lib/Core/UserSearcher.h"
#include "../../lib/Core/PTree.h"

#include <vector>

#include "gueststate.h"
#include "guestcpustate.h"
#include "genllvm.h"
#include "vexhelpers.h"
#include "vexxlate.h"
#include "vexsb.h"
#include "vexfcache.h"
#include "static/Sugar.h"

#include "ExecutorVex.h"

using namespace klee;
using namespace llvm;

extern bool WriteTraces;
extern bool UseEquivalentStateEliminator;

ExecutorVex::ExecutorVex(
	const InterpreterOptions &opts,
	InterpreterHandler *ih,
	GuestState	*in_gs)
: Executor(opts, ih),
  gs(in_gs)
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

	std::list<Module*> l = theVexHelpers->getModules();
	foreach (it, l.begin(), l.end()) {
		kmodule->addModule(*it);
	}

	if (UseEquivalentStateEliminator)
		stateManager->setupESE(this, kmodule, state);

	fprintf(stderr, "RUN IMAGE\n");
	prepState(state, init_func);
	initializeGlobals(*state);

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


void ExecutorVex::prepState(ExecutionState* state, Function* f)
{
	setupRegisterContext(state, f);
	setupProcessMemory(state, f);
}

void ExecutorVex::setupProcessMemory(ExecutionState* state, Function* f)
{
	std::list<GuestMemoryRange*> memmap(gs->getMemoryMap());
	foreach (it, memmap.begin(), memmap.end()) {
		GuestMemoryRange	*gmr;
		MemoryObject		*mmap_mo;
		ObjectState		*mmap_os;
		unsigned int		len;
		const char		*data;

		gmr = *it;
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
	unsigned int state_regctx_sz = gs->getCPUState()->getStateSize();

	state_regctx_mo = memory->allocate(
		state_regctx_sz,
		false, true,
		f->begin()->begin(), state);
	args.push_back(state_regctx_mo->getBaseExpr());

	if (symPathWriter) state->symPathOS = symPathWriter->open();
	if (statsTracker) statsTracker->framePushed(*state, 0);

	assert(args.size() == f->arg_size() && "wrong number of arguments");

	KFunction* kf = kmodule->getKFunction(f);
	for (unsigned i = 0, e = f->arg_size(); i != e; ++i)
		bindArgument(kf, i, *state, args[i]);

	if (!state_regctx_mo) return;

	ObjectState *state_regctx_os;
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

	fprintf(stderr, "NEW FUNC\n");
	f->dump();

	/* need to know func -> vsb to compute func's guest address */
	vsb = xlate_cache->getCachedVSB(guest_addr);
	assert (vsb && "Dropped VSB too early?");
	func2vsb_table[(uint64_t)f] = vsb;


	/* stupid kmodule stuff */
	kf = kmodule->addFunction(f);
	bindKFuncConstants(kf);
	bindModuleConstTable(); /* XXX slow */

	return f;
}

void ExecutorVex::executeInstruction(
	ExecutionState &state, KInstruction *ki)
{
	ki->inst->dump();
	Executor::executeInstruction(state, ki);
}

/* need to hand roll our own instRet because we want to be able to
 * jump between super blocks based on ret values */
void ExecutorVex::instRet(ExecutionState &state, KInstruction *ki)
{
	Function		*cur_func;
	VexSB			*vsb;

// no nested ret??
//	if (state.stack.size() > 1) {
//		/* if returning from a generated call (e.g. outcalls)  */
//		fprintf(stderr, "RET FORM NESTED\n");
//		Executor::instRetFromNested(state, ki);
//		return;
//	}

	/* need to trapeze between VSB's; depending on exit type,
	 * translate VSB exits into LLVM instructions on the fly */

	cur_func = (state.stack.back()).kf->function;
	fprintf(stderr, "CURRENTLY ON %s\n", cur_func->getNameStr().c_str());

	vsb = func2vsb_table[(uintptr_t)cur_func];
	assert (vsb && "Could not translate current function to VSB");


	fprintf(stderr, "pc: %p\n", vsb->getGuestAddr());
	fprintf(stderr, "vsb stats %d %d %d\n",
		vsb->isCall(),
		vsb->isSyscall(),
		vsb->isReturn());

	if (vsb->isCall()) {
		fprintf(stderr, "CALL!\n");
		handleXferCall(state, ki);
		return;
	} else if (vsb->isSyscall()) {
		fprintf(stderr, "SYSCALL!\n");
		assert (0 == 1 && "HANDLE SYSCALL");
		handleXferSyscall(state, ki);
		return;
	} else if (vsb->isReturn()) {
		fprintf(stderr, "RET!\n");
		assert (0 == 1);
		handleXferReturn(state, ki);
		return;
	} else {
		handleXferJmp(state, ki);
		return;
	}

	ref<Expr> result = ConstantExpr::alloc(0, Expr::Bool);
	result = eval(ki, 0, state).value;

	fprintf(stderr, "terminating initial stack frame\n");
	fprintf(stderr, "result: ");
	result->dump();
	terminateStateOnExit(state);
	return;
}

void ExecutorVex::handleXferJmp(ExecutionState& state, KInstruction* ki)
{
	ref<Expr>			v = eval(ki, 0, state).value;
	ExecutionState			*free = &state;
	bool				first = true;

	/* this is mostly a copy of Executor's implementation of 
	 * the symbolic function call; probably should try to merge the two */
	do {
		ref<ConstantExpr>	value;
		uint64_t		addr;
		Function		*f;
		StatePair 		res;
		bool			success;
		
		success = solver->getValue(*free, v, value);
		assert(success && "FIXME: Unhandled solver failure");
		(void) success;

		res = fork(*free, EqExpr::create(v, value), true);
		if (!res.first) {
			first = false;
			free = res.second;
			continue;
		}

		addr = value->getZExtValue();
		fprintf(stderr, "XXX GOT VALUE: %p\n", addr);

		f = getFuncFromAddr(addr);
		assert (f != NULL && "BAD FUNCTION TO JUMP TO");

		// Don't give warning on unique resolution
		if (res.second || !first) {
			klee_warning_once(
				(void*) (unsigned long) addr,
				"resolved symbolic function pointer to: %s",
				f->getName().data());
		}

		fprintf(stderr, "AAAAAAAAAAAAAAAA\n");
		assert (0 == 1);
//		bindArgument(kf, i, *res.first, state_regctx_mo->getBaseExpr());
		fprintf(stderr, "DONE WITH AAAAAAAAAAL\n");

		first = false;
		free = res.second;
	} while (free);
}

/* xfers are done with an address in the return value of the next place to 
 * jump.  g=f(x) => g(x) -> f(x). (g directly follows f) */
void ExecutorVex::handleXferCall(ExecutionState& state, KInstruction* ki)
{
	std::vector< ref<Expr> > 	args;
	ref<Expr>			v = eval(ki, 0, state).value;
	ExecutionState			*free = &state;
	bool				first = true;

	args.push_back(state_regctx_mo->getBaseExpr());

	/* this is mostly a copy of Executor's implementation of 
	 * the symbolic function call; probably should try to merge the two */
	do {
		ref<ConstantExpr>	value;
		uint64_t		addr;
		Function		*f;
		StatePair 		res;
		bool			success;
		
		success = solver->getValue(*free, v, value);
		assert(success && "FIXME: Unhandled solver failure");
		(void) success;

		res = fork(*free, EqExpr::create(v, value), true);
		if (!res.first) {
			first = false;
			free = res.second;
			continue;
		}

		addr = value->getZExtValue();
		fprintf(stderr, "GOT VALUE: %p\n", addr);

		f = getFuncFromAddr(addr);
		assert (f != NULL && "BAD FUNCTION TO JUMP TO");

		// Don't give warning on unique resolution
		if (res.second || !first) {
			klee_warning_once(
				(void*) (unsigned long) addr,
				"resolved symbolic function pointer to: %s",
				f->getName().data());
		}

		fprintf(stderr, "EXECUTE THE CALL\n");
		executeCall(*res.first, ki, f, args);
		fprintf(stderr, "DONE WITH EXECUTECALL\n");

		first = false;
		free = res.second;
	} while (free);
	fprintf(stderr, "HERE WE GOOOOO\n");
}

void ExecutorVex::handleXferSyscall(
	ExecutionState& state, KInstruction* ki)
{
	assert (0 == 1);
}

void ExecutorVex::handleXferReturn(
	ExecutionState& state, KInstruction* ki)
{
	assert (0 == 1);
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

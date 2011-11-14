#include <static/Sugar.h>
#include "klee/ExeStateBuilder.h"
#include "llvm/Module.h"
#include "llvm/LLVMContext.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Support/CommandLine.h"
#include "klee/Common.h"
#include "klee/util/ExprPPrinter.h"
#include "UserSearcher.h"
#include "SpecialFunctionHandler.h"
#include "StatsTracker.h"
#include "TimingSolver.h"
#include "ExeStateManager.h"
#include "ExternalDispatcher.h"
#include "PTree.h"
#include "HeapMM.h"
#include <sstream>
#include "klee/Internal/Module/KModule.h"

#include "ExecutorBC.h"

using namespace klee;
using namespace llvm;

namespace {
  cl::opt<bool>
  UseAsmAddresses("use-asm-addresses",
                  cl::init(false));

  cl::opt<bool>
  NoExternals("no-externals", 
           cl::desc("Do not allow external functin calls"));

  cl::opt<bool>
  AllowExternalSymCalls("allow-external-sym-calls",
                        cl::init(false));

  cl::opt<bool>
  SuppressExternalWarnings("suppress-external-warnings");
}

ExecutorBC::ExecutorBC(InterpreterHandler *ie)
: Executor(ie)
, specialFunctionHandler(0)
, externalDispatcher(new ExternalDispatcher())
{
	assert (kmodule == NULL);
}

ExecutorBC::~ExecutorBC(void)
{
  	if (specialFunctionHandler) delete specialFunctionHandler;
	if (externalDispatcher)  delete externalDispatcher;
	if (kmodule) delete kmodule;
}

const Module* ExecutorBC::setModule(
	llvm::Module *module,
	const ModuleOptions &opts)
{
	// XXX gross
	assert(!kmodule && module && "can only register one module");

	kmodule = new KModule(module);

	target_data = kmodule->targetData;

	// Initialize the context.
	Context::initialize(
		target_data->isLittleEndian(),
		(Expr::Width) target_data->getPointerSizeInBits());

	specialFunctionHandler = new SpecialFunctionHandler(this);

	specialFunctionHandler->prepare();
	kmodule->prepare(opts, interpreterHandler);
	specialFunctionHandler->bind();

	if (StatsTracker::useStatistics()) {
		statsTracker = new StatsTracker(
		*this,
		kmodule,
		interpreterHandler->getOutputFilename("assembly.ll"),
		opts.ExcludeCovFiles,
		userSearcherRequiresMD2U());
	}

	return module;
}

void ExecutorBC::runFunctionAsMain(
	Function *f, int argc, char **argv, char **envp)
{
	ExecutionState *state;
	KFunction *kf;
	
	kf = kmodule->getKFunction(f);
	assert(kf && "No KFunction for LLVM function??");

	// force deterministic initialization of memory objects
	srand(1);
	srandom(1);

	state = ExeStateBuilder::create(kf);

	setupArgv(state, f, argc, argv, envp);

	initializeGlobals(*state);

	processTree = new PTree(state);
	state->ptreeNode = processTree->root;

	run(*state);

	delete processTree;
	processTree = 0;

	// hack to clear memory objects
	delete memory;
	memory = MemoryManager::create();

	globalObjects.clear();
	globalAddresses.clear();

	if (statsTracker) statsTracker->done();
}

void ExecutorBC::initializeGlobals(ExecutionState &state)
{
	Module *m;
	
	m = kmodule->module;

	if (m->getModuleInlineAsm() != "")
		klee_warning("executable has module level assembly (ignoring)");

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
		if (	f->hasExternalWeakLinkage() && 
			!externalDispatcher->resolveSymbol(f->getNameStr())) {
			addr = Expr::createPointer(0);
			std::cerr << "KLEE:ERROR: couldn't find symbol for weak linkage of " 
				"global function: " << f->getNameStr() << std::endl;
		} else {
			addr = Expr::createPointer((unsigned long) (void*) f);
			legalFunctions.insert(
				(uint64_t) (unsigned long) (void*) f);
		}

		globalAddresses.insert(std::make_pair(f, addr));
	}

// Disabled, we don't want to promote use of live externals.
#ifdef HAVE_CTYPE_EXTERNALS
#ifndef WINDOWS
#ifndef DARWIN
	/* From /usr/include/errno.h: it [errno] is a per-thread variable. */
	int *errno_addr = __errno_location();
	addExternalObject(state, (void *)errno_addr, sizeof *errno_addr, false);

	/* from /usr/include/ctype.h:
	These point into arrays of 384, so they can be indexed by any `unsigned
	char' value [0,255]; by EOF (-1); or by any `signed char' value
	[-128,-1).  ISO C requires that the ctype functions work for `unsigned */
	const uint16_t **addr = __ctype_b_loc();
	addExternalObject(
		state, (void *)(*addr-128), 384 * sizeof **addr, true);
	addExternalObject(state, addr, sizeof(*addr), true);

	const int32_t **lower_addr = __ctype_tolower_loc();
	addExternalObject(
		state, (void *)(*lower_addr-128), 384 * sizeof **lower_addr, true);
	addExternalObject(state, lower_addr, sizeof(*lower_addr), true);

	const int32_t **upper_addr = __ctype_toupper_loc();
	addExternalObject(
		state, (void *)(*upper_addr-128), 384 * sizeof **upper_addr, true);
	addExternalObject(state, upper_addr, sizeof(*upper_addr), true);
#endif
#endif
#endif

	// allocate and initialize globals, done in two passes since we may
	// need address of a global in order to initialize some other one.

	// allocate memory objects for all globals
	foreach (i, m->global_begin(), m->global_end()) {
		if (i->isDeclaration())
			allocGlobalVariableDecl(state, *i);
		else
			allocGlobalVariableNoDecl(state, *i);
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
}

void ExecutorBC::setupArgv(
	ExecutionState* state,
	Function *f,
	int argc, char **argv, char **envp)
{
	std::vector<ref<Expr> > arguments;
	int envc;
	MemoryObject *argvMO = 0;
	unsigned NumPtrBytes = Context::get().getPointerWidth() / 8;

	// In order to make uclibc happy and be closer to what the system is
	// doing we lay out the environments at the end of the argv array
	// (both are terminated by a null). There is also a final terminating
	// null that uclibc seems to expect, possibly the ELF header?

	for (envc=0; envp[envc]; ++envc) ;

	Function::arg_iterator ai = f->arg_begin(), ae = f->arg_end();
	if (ai!=ae) {
		arguments.push_back(ConstantExpr::alloc(argc, Expr::Int32));

		if (++ai==ae) goto done_ai;

		argvMO = memory->allocate(
			(argc+1+envc+1+1) * NumPtrBytes, 
			false, true,
			f->begin()->begin(), state);

		arguments.push_back(argvMO->getBaseExpr());

		if (++ai==ae) goto done_ai;

		uint64_t envp_start;
		envp_start = argvMO->address + (argc+1)*NumPtrBytes;
		arguments.push_back(
			Expr::createPointer(envp_start));
		if (++ai!=ae)
			klee_error("invalid main function (expect 0-3 arguments)");
	}

done_ai:

	if (symPathWriter) state->symPathOS = symPathWriter->open();
	if (statsTracker) statsTracker->framePushed(*state, 0);

	assert(arguments.size() == f->arg_size() && "wrong number of arguments");

	KFunction* kf = kmodule->getKFunction(f);
	for (unsigned i = 0, e = f->arg_size(); i != e; ++i)
		state->bindArgument(kf, i, arguments[i]);

	if (!argvMO) return;

	ObjectState *argvOS = state->bindMemObj(argvMO);

	for (int i=0; i<argc+1+envc+1+1; i++) {
		ObjectState	*os;

		if (i==argc || i>=argc+1+envc) {
			state->write(
				argvOS,
				i * NumPtrBytes,
				Expr::createPointer(0));
			continue;
		}

		char *s = i<argc ? argv[i] : envp[i-(argc+1)];
		int j, len = strlen(s);


		os = state->allocate(len+1, false, true, state->pc->inst);
		assert (os != NULL);

		for (j=0; j<len+1; j++)
			state->write8(os, j, s[j]);

		state->write(
			argvOS, i * NumPtrBytes,
			os->getObject()->getBaseExpr());
	}
}

// XXX shoot me
static const char *okExternalsList[] = { "printf", 
                                         "fprintf", 
                                         "puts",
                                         "getpid" };
static std::set<std::string> okExternals(
	okExternalsList,
	okExternalsList + (sizeof(okExternalsList)/sizeof(okExternalsList[0])));

void ExecutorBC::callExternalFunction(
	ExecutionState &state,
	KInstruction *target,
	Function *function,
	std::vector< ref<Expr> > &arguments)
{
	// check if specialFunctionHandler wants it
	if (specialFunctionHandler->handle(state, function, target, arguments))
		return;

	if (NoExternals && !okExternals.count(function->getName())) {
		std::cerr << "KLEE:ERROR: Calling not-OK external function : "
			<< function->getNameStr() << "\n";
		terminateStateOnError(state, "externals disallowed", "user.err");
		return;
	}

	// normal external function handling path
	// allocate 128 bits for each argument (+return value) to support fp80's;
	// we could iterate through all the arguments first and determine the exact
	// size we need, but this is faster, and the memory usage isn't significant.
	uint64_t *args;
	args = (uint64_t*) alloca(2*sizeof(*args) * (arguments.size() + 1));
	memset(args, 0, 2 * sizeof(*args) * (arguments.size() + 1));
	unsigned wordIndex = 2;
	foreach (ai, arguments.begin(), arguments.end()) {
		// DAR: for printf, etc., don't terminate paths on symbolic calls
		if (	AllowExternalSymCalls
			|| okExternals.count(function->getName()))
		{
			// don't bother checking uniqueness
			ref<ConstantExpr> ce;
			bool success = solver->getValue(state, *ai, ce);
			assert(success && "FIXME: Unhandled solver failure");
			(void) success;
			ce->toMemory(&args[wordIndex]);
			wordIndex += (ce->getWidth()+63)/64;
		} else {
			ref<Expr> arg = toUnique(state, *ai);
			if (ConstantExpr *ce = dyn_cast<ConstantExpr>(arg)) {
				// XXX kick toMemory functions from here
				ce->toMemory(&args[wordIndex]);
				wordIndex += (ce->getWidth()+63)/64;
			} else {
				terminateStateOnExecError(
					state, 
					"external call with symbolic argument: "+
					function->getName());
				return;
			}
		}
	}

	state.addressSpace.copyOutConcretes();

	if (!SuppressExternalWarnings) {
		std::ostringstream os;
		os << "calling external: " << function->getNameStr() << "(";
		for (unsigned i=0; i<arguments.size(); i++) {
			os << arguments[i];
			if (i != arguments.size()-1) os << ", ";
		}
		os << ")";

//		if (AllExternalWarnings)
//			klee_warning("%s", os.str().c_str());
//		else
			klee_warning_once(function, "%s", os.str().c_str());
	}

	bool success;
	success = externalDispatcher->executeCall(function, target->inst, args);
	if (!success) {
		terminateStateOnError(
			state,
			"failed external call: " + function->getName(),
			"external.err");
		return;
	}

	if (!state.addressSpace.copyInConcretes()) {
		terminateStateOnError(
			state,
			"external modified read-only object",
			"external.err");
		return;
	}

	const Type *resultType = target->inst->getType();
	if (resultType != Type::getVoidTy(getGlobalContext())) {
		ref<Expr> e = ConstantExpr::fromMemory(
			(void*) args,
			getWidthForLLVMType(resultType));
		state.bindLocal(target, e);
	}
}

void ExecutorBC::run(ExecutionState &initialState)
{
	bindModuleConstants();
	Executor::run(initialState);
}

void ExecutorBC::allocGlobalVariableDecl(
  ExecutionState& state, const GlobalVariable& gv)
{
	MemoryObject *mo;
	ObjectState *os;
	assert (gv.isDeclaration());
	// FIXME: We have no general way of handling unknown external
	// symbols. If we really cared about making external stuff work
	// better we could support user definition, or use the EXE style
	// hack where we check the object file information.

	const Type *ty = gv.getType()->getElementType();
	uint64_t size = target_data->getTypeStoreSize(ty);

	// XXX - DWD - hardcode some things until we decide how to fix.
#ifndef WINDOWS
	if (gv.getName() == "_ZTVN10__cxxabiv117__class_type_infoE") {
		size = 0x2C;
	} else if (gv.getName() == "_ZTVN10__cxxabiv120__si_class_type_infoE") {
		size = 0x2C;
	} else if (gv.getName() == "_ZTVN10__cxxabiv121__vmi_class_type_infoE") {
		size = 0x2C;
	}
#endif

	if (size == 0) {
		std::cerr << "Unable to find size for global variable: " 
		<< gv.getName().data()
		<< " (use will result in out of bounds access)\n";
	}

	os = state.allocate(size, false, true, &gv);
	mo = os->getObject();
	globalObjects.insert(std::make_pair(&gv, mo));
	globalAddresses.insert(std::make_pair(&gv, mo->getBaseExpr()));

	// Program already running = object already initialized.  Read
	// concrete value and write it to our copy.
	if (size == 0) return;

	void *addr;
	if (gv.getName() == "__dso_handle") {
		extern void *__dso_handle __attribute__ ((__weak__));
		addr = &__dso_handle; // wtf ?
	} else {
		addr = externalDispatcher->resolveSymbol(gv.getNameStr());
	}
	if (!addr) {
		klee_warning(
			"ERROR: unable to load symbol(%s) while initializing globals.", 
			gv.getName().data());
	} else {
		for (unsigned offset=0; offset < mo->size; offset++) {
			//os->write8(offset, ((unsigned char*)addr)[offset]);
			state.write8(os, offset, ((unsigned char*)addr)[offset]);
		}
	}
}

/* XXX needs a better name */
void ExecutorBC::allocGlobalVariableNoDecl(
	ExecutionState& state,
	const GlobalVariable& gv)
{
	const Type *ty = gv.getType()->getElementType();
	uint64_t size = target_data->getTypeStoreSize(ty);
	MemoryObject *mo = 0;
	ObjectState *os = 0;

	if (UseAsmAddresses && gv.getName()[0]=='\01') {
		char *end;
		uint64_t address = ::strtoll(gv.getNameStr().c_str()+1, &end, 0);

		if (end && *end == '\0') {
		// We can't use the PRIu64 macro here for some reason, so we have to
		// cast to long long unsigned int to avoid compiler warnings.
			klee_message(
			"NOTE: allocated global at asm specified address: %#08llx"
			" (%llu bytes)",
			(long long unsigned int) address,
			(long long unsigned int) size);
		//      mo = memory->allocateFixed(address, size, &*i, &state);
			os = state.allocateFixed(address, size, &gv);
			mo = os->getObject();
			mo->isUserSpecified = true; // XXX hack;
		}
	}

	//if (!mo) mo = memory->allocate(size, false, true, &*i, &state);
	if (os == NULL) os = state.allocate(size, false, true, &gv);
	assert(os && "out of memory");

	mo = os->getObject();
	globalObjects.insert(std::make_pair(&gv, mo));
	globalAddresses.insert(std::make_pair(&gv, mo->getBaseExpr()));

	if (!gv.hasInitializer()) os->initializeToRandom();
}

Function* ExecutorBC::getFuncByAddr(uint64_t addr)
{
	if (legalFunctions.count(addr) == 0)
		return NULL;

	/* hurr */
	return (Function*) addr;
}

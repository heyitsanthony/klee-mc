#include <llvm/Module.h>
#include <llvm/LLVMContext.h>
#include <llvm/Target/TargetData.h>
#include <llvm/Support/CommandLine.h>
#include <sstream>

#include "static/Sugar.h"
#include "klee/ExeStateBuilder.h"
#include "klee/Common.h"
#include "klee/util/ExprPPrinter.h"
#include "SpecialFunctionHandler.h"
#include "StatsTracker.h"
#include "StateSolver.h"
#include "ExternalDispatcher.h"
#include "PTree.h"
#include "HeapMM.h"
#include "Globals.h"
#include "klee/Internal/Module/KModule.h"

#include "ExecutorBC.h"

using namespace klee;
using namespace llvm;

namespace {
	cl::opt<bool>
	NoExternals("no-externals",  cl::desc("Disallow external func calls"));

	cl::opt<bool>
	AllowExternalSymCalls("allow-external-sym-calls", cl::init(false));

	cl::opt<bool>
	SuppressExternalWarnings("suppress-external-warnings");
}

ExecutorBC::ExecutorBC(InterpreterHandler *ie)
: Executor(ie)
, externalDispatcher(new ExternalDispatcher())
{
	assert (kmodule == NULL);
}

ExecutorBC::~ExecutorBC(void)
{
	if (externalDispatcher)  delete externalDispatcher;
	if (kmodule) delete kmodule;
}

const Module* ExecutorBC::setModule(
	llvm::Module *module,
	const ModuleOptions &opts)
{
	// XXX gross
	assert(!kmodule && module && "can only register one module");

	kmodule = new KModule(module, opts);

	target_data = kmodule->targetData;

	// Initialize the context.
	Context::initialize(
		target_data->isLittleEndian(),
		(Expr::Width) target_data->getPointerSizeInBits());

	sfh = new SpecialFunctionHandler(this);

	sfh->prepare();
	kmodule->prepare(interpreterHandler);
	sfh->bind();

	if (StatsTracker::useStatistics()) {
		statsTracker = new StatsTracker(
			*this,
			kmodule,
			interpreterHandler->getOutputFilename("assembly.ll"),
			opts.ExcludeCovFiles);
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

	globals = new Globals(kmodule, state, externalDispatcher);

	pathTree = new PTree(state);
	state->ptreeNode = pathTree->root;

	run(*state);

	delete pathTree;
	pathTree = 0;

	delete globals;
	globals = NULL;

	// hack to clear memory objects
	delete memory;
	memory = MemoryManager::create();

	if (statsTracker) statsTracker->done();
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

	ObjectState *argvOS = state->bindMemObjWriteable(argvMO);

	for (int i=0; i<argc+1+envc+1+1; i++) {
		ObjectPair	op;
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


		op = state->allocateGlobal(len+1, state->pc->getInst());
		assert (op_os(op) != NULL);
		os = state->addressSpace.getWriteable(op);

		for (j=0; j<len+1; j++)
			state->write8(os, j, s[j]);

		state->write(argvOS, i * NumPtrBytes, op_mo(op)->getBaseExpr());
	}
}

// XXX shoot me
static const char *okExternalsList[] =
{ "printf", "fprintf", "puts", "getpid" };

static std::set<std::string> okExternals(
	okExternalsList,
	okExternalsList + (sizeof(okExternalsList)/sizeof(okExternalsList[0])));

void ExecutorBC::callExternalFunction(
	ExecutionState &state,
	KInstruction *target,
	Function *function,
	std::vector< ref<Expr> > &arguments)
{
	uint64_t	*args;
	unsigned	wordIndex = 2;

	// check if sfh wants it
	if (sfh->handle(state, function, target, arguments))
		return;

	if (NoExternals && !okExternals.count(function->getName().str())) {
		std::cerr << "KLEE:ERROR: Calling not-OK external function : "
			<< function->getName().str() << "\n";
		terminateStateOnError(state, "externals disallowed", "user.err");
		return;
	}

	// normal external function handling path
	// allocate 128 bits for each argument (+return value)
	// to support fp80's;
	// we could iterate through all the arguments first
	// and determine the exact size we need, but this is faster,
	// and the memory usage isn't significant.
	args = (uint64_t*) alloca(2*sizeof(*args) * (arguments.size() + 1));
	memset(args, 0, 2 * sizeof(*args) * (arguments.size() + 1));
	if (	AllowExternalSymCalls
		|| okExternals.count(function->getName().str()))
	{
		// DAR: for printf, etc., don't terminate paths on symbolic calls
		// don't bother checking uniqueness
		foreach (ai, arguments.begin(), arguments.end()) {
			ref<ConstantExpr> ce;
			bool success = solver->getValue(state, *ai, ce);
			assert(success && "FIXME: Unhandled solver failure");
			(void) success;
			ce->toMemory(&args[wordIndex]);
			wordIndex += (ce->getWidth()+63)/64;
		}
	} else {
		foreach (ai, arguments.begin(), arguments.end()) {
			ref<Expr> arg = toUnique(state, *ai);
			if (ConstantExpr *ce = dyn_cast<ConstantExpr>(arg)) {
				// XXX kick toMemory functions from here
				ce->toMemory(&args[wordIndex]);
				wordIndex += (ce->getWidth()+63)/64;
			} else {
				terminateStateOnExecError(
					state, 
					"external call with symbolic arg: "+
					function->getName().str());
				return;
			}
		}
	}

	state.addressSpace.copyOutConcretes();

	if (!SuppressExternalWarnings) {
		std::ostringstream os;
		os << "calling external: " << function->getName().str() << "(";
		for (unsigned i=0; i<arguments.size(); i++) {
			os << arguments[i];
			if (i != arguments.size()-1) os << ", ";
		}
		os << ")";

		klee_warning_once(function, "%s", os.str().c_str());
	}

	bool ok;
	ok = externalDispatcher->executeCall(function, target->getInst(), args);
	if (!ok) {
		terminateStateOnError(
			state,
			"failed external call: " + function->getName().str(),
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

	Type *resultType = target->getInst()->getType();
	if (resultType->isVoidTy() == false) {
		ref<Expr> e = ConstantExpr::fromMemory(
			(void*) args,
			kmodule->getWidthForLLVMType(resultType));
		state.bindLocal(target, e);
	}
}

Function* ExecutorBC::getFuncByAddr(uint64_t addr)
{
	if (!globals->isLegalFunction(addr))
		return NULL;

	/* hurr */
	return (Function*) addr;
}

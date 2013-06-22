#include "../../lib/Core/Context.h"
#include "../../lib/Core/SpecialFunctionHandler.h"
#include <llvm/IR/DataLayout.h>
#include "../../lib/Core/StatsTracker.h"
#include "../../lib/Core/Globals.h"
#include "ExecutorJ.h"
#include "KModuleJ.h"

using namespace klee;


ExecutorJ::ExecutorJ(InterpreterHandler *ie)
: Executor(ie)
{
	assert (kmodule == NULL);
}


const llvm::Module* ExecutorJ::setModule(
	llvm::Module *mod,
	const ModuleOptions &opts)
{
	kmodule = new KModuleJ(mod, opts);
	data_layout = kmodule->dataLayout;

	// Initialize the context.
	Context::initialize(
		data_layout->isLittleEndian(),
		(Expr::Width) data_layout->getPointerSizeInBits());

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

	return mod;
}

void ExecutorJ::runAndroid(void)
{
	assert (0 == 1 && "STUB");
}

llvm::Function* ExecutorJ::getFuncByAddr(uint64_t addr)
{
	if (!globals->isLegalFunction(addr))
		return NULL;

	/* hurr */
	return (llvm::Function*) addr;
}

ExecutorJ::~ExecutorJ(void)
{
	if (kmodule) delete kmodule;
	kmodule = NULL;
}

void ExecutorJ::callExternalFunction(
	ExecutionState &state,
	KInstruction *target,
	llvm::Function *function,
	std::vector< ref<Expr> > &arguments)
{
	// check if sfh wants it
	if (sfh->handle(state, function, target, arguments))
		return;

	TERMINATE_ERROR(this, state, "disallowed external", "user.err");
}

#ifndef EXECUTORJ_H
#define EXECUTORJ_H

#include "../../lib/Core/Executor.h"

namespace klee
{
class ExecutorJ : public Executor
{
public:
	ExecutorJ(InterpreterHandler *ie);
	virtual ~ExecutorJ(void);
	const llvm::Module * setModule(
		llvm::Module *module,
		const ModuleOptions &opts);

	void runFunctionAsMain(
		llvm::Function *f,
		int argc,
		char **argv,
		char **envp) { assert (0 == 1 && "STUB"); abort(); }

	llvm::Function* getFuncByAddr(uint64_t addr);

	void callExternalFunction(
		ExecutionState &state,
		KInstruction *target,
		llvm::Function *function,
		std::vector< ref<Expr> > &arguments);

	void runAndroid(void);
private:
};
}

#endif

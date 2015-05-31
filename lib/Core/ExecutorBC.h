/* executor for bitcode environment */
#ifndef KLEE_EXECUTOR_BC_H
#define KLEE_EXECUTOR_BC_H

#include "Executor.h"

namespace klee {  
class KModule;

class ExecutorBC : public Executor
{
public:
	ExecutorBC(InterpreterHandler *ie);
	virtual ~ExecutorBC(void);

	const llvm::Module * setModule(
		llvm::Module *module,
		const ModuleOptions &opts);

	void runFunctionAsMain(
		llvm::Function *f,
		int argc,
		char **argv,
		char **envp);

protected:
	virtual void callExternalFunction(
		ExecutionState &state,
		KInstruction *target,
		llvm::Function *function,
		std::vector< ref<Expr> > &arguments);

  	virtual llvm::Function* getFuncByAddr(uint64_t addr);

private:
	void setupArgv(
		ExecutionState* state,
		llvm::Function *f,
		int argc, char **argv, char **envp);

	void setExternalErrno(ExecutionState& es);

 	std::unique_ptr<ExternalDispatcher> externalDispatcher;
};

}
#endif

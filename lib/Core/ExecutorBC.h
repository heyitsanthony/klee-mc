/* executor for bitcode environment */
#ifndef KLEE_EXECUTOR_BC_H
#define KLEE_EXECUTOR_BC_H

#include "Executor.h"

namespace klee {  
class KModule;

class ExecutorBC : public Executor
{
public:
	ExecutorBC(const InterpreterOptions &opts, InterpreterHandler *ie);
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
  	virtual void run(ExecutionState &initialState);

	virtual void callExternalFunction(
		ExecutionState &state,
		KInstruction *target,
		llvm::Function *function,
		std::vector< ref<Expr> > &arguments);

  	virtual llvm::Function* getFuncByAddr(uint64_t addr);
private:
	void allocGlobalVariableDecl(
		ExecutionState& state,
		const llvm::GlobalVariable& gv);
	void allocGlobalVariableNoDecl(
		ExecutionState& s,
		const llvm::GlobalVariable& gv);

	void initializeGlobals(ExecutionState &state);

	void setupArgv(
		ExecutionState* state,
		llvm::Function *f,
		int argc, char **argv, char **envp);

	SpecialFunctionHandler *specialFunctionHandler;
 	ExternalDispatcher *externalDispatcher;

	/// The set of legal function addresses, used to validate function
	/// pointers. We use the actual Function* address as the function address.
	std::set<uint64_t> legalFunctions;
};

}
#endif

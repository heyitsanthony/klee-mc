#ifndef KLEE_EXECUTOR_VEX_H
#define KLEE_EXECUTOR_VEX_H

#include "../../lib/Core/Executor.h"
#include <assert.h>

class GuestState;
class VexXlate;

namespace klee {  
class KModule;

class ExecutorVex : public Executor
{
public:
	ExecutorVex(
		const InterpreterOptions &opts,
		InterpreterHandler *ie,
		GuestState* gs);
	virtual ~ExecutorVex(void);

	const llvm::Module * setModule(
		llvm::Module *module,
		const ModuleOptions &opts) { assert (0 == 1 && "BUSTED"); }

	void runFunctionAsMain(
		llvm::Function *f,
		int argc,
		char **argv,
		char **envp) { assert (0 == 1 && "LOL"); }

	void runImage(void);

	const KModule* getKModule(void) const { return kmodule; }

protected:
  	virtual void executeInstruction(
		ExecutionState &state, KInstruction *ki) { assert (0 == 1 && "STUB"); }
	virtual void executeCallNonDecl(
		ExecutionState &state, 
		KInstruction *ki,
		Function *f,
		std::vector< ref<Expr> > &arguments) { assert (0 == 1 && "STUB"); }

  	virtual void run(ExecutionState &initialState) { assert (0 == 1 && "STUB"); }

	virtual const Cell& eval(
		KInstruction *ki,
		unsigned index,
		ExecutionState &state) const { assert (0 == 1 && "STUB"); }

	virtual llvm::Function* getCalledFunction(
		llvm::CallSite &cs, ExecutionState &state) { assert (0 == 1 && "STUB"); }

	virtual void callExternalFunction(
		ExecutionState &state,
		KInstruction *target,
		Function *function,
		std::vector< ref<Expr> > &arguments) { assert (0 == 1 && "STUB"); }
private:
	KModule		*kmodule;
	VexXlate	*xlate;
	GuestState	*gs;
};

}
#endif

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

	/// Allocate and bind a new object in a particular state. NOTE: This
	/// function may fork.
	///
	/// \param isLocal Flag to indicate if the object should be
	/// automatically deallocated on function return (this also makes it
	/// illegal to free directly).
	///
	/// \param target Value at which to bind the base address of the new
	/// object.
	///
	/// \param reallocFrom If non-zero and the allocation succeeds,
	/// initialize the new object from the given one and unbind it when
	/// done (realloc semantics). The initialized bytes will be the
	/// minimum of the size of the old and new objects, with remaining
	/// bytes initialized as specified by zeroMemory.
	void executeAlloc(
		ExecutionState &state,
		ref<Expr> size,
		bool isLocal,
		KInstruction *target,
		bool zeroMemory=false,
		const ObjectState *reallocFrom=0);

	void executeAllocSymbolic(
		ExecutionState &state,
		ref<Expr> size,
		bool isLocal,
		KInstruction *target,
		bool zeroMemory,
		const ObjectState *reallocFrom);

	void executeAllocConst(
		ExecutionState &state,
		ConstantExpr* CE,
		bool isLocal,
		KInstruction *target,
		bool zeroMemory,
		const ObjectState *reallocFrom);

	/// Free the given address with checking for errors. If target is
	/// given it will be bound to 0 in the resulting states (this is a
	/// convenience for realloc). Note that this function can cause the
	/// state to fork and that \ref state cannot be safely accessed
	/// afterwards.
	void executeFree(
		ExecutionState &state,
		ref<Expr> address,
		KInstruction *target = 0);
protected:
  	virtual void executeInstruction(
		ExecutionState &state, KInstruction *ki);

  	virtual void run(ExecutionState &initialState);

	virtual const Cell& eval(
		KInstruction *ki,
		unsigned index,
		ExecutionState &state) const;

	virtual void callExternalFunction(
		ExecutionState &state,
		KInstruction *target,
		llvm::Function *function,
		std::vector< ref<Expr> > &arguments);
private:
	void allocGlobalVariableDecl(
		ExecutionState& state,
		const llvm::GlobalVariable& gv);
	void allocGlobalVariableNoDecl(
		ExecutionState& s,
		const llvm::GlobalVariable& gv);

	/// bindModuleConstants - Initialize the module constant table.
	void bindModuleConstants(void);

	void initializeGlobals(ExecutionState &state);

	void setupArgv(
		ExecutionState* state,
		llvm::Function *f,
		int argc, char **argv, char **envp);

	SpecialFunctionHandler *specialFunctionHandler;
 	ExternalDispatcher *externalDispatcher;

};

}
#endif

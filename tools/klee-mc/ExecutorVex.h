#ifndef KLEE_EXECUTOR_VEX_H
#define KLEE_EXECUTOR_VEX_H

#include "collection.h"
#include "guest.h"
#include "../../lib/Core/Executor.h"

#include <iostream>
#include <assert.h>

namespace llvm
{
class Function;
class GlobalVariable;
}

namespace klee {
class KModule;
class KFunction;
class MemoryObject;
class ObjectState;
class SyscallSFH;
class SysModel;
class KModuleVex;

#define es2esv(x)	static_cast<ExeStateVex&>(x)
#define GETREGOBJ(x)	es2esv(x).getRegObj()
#define GETREGOBJRO(x)	es2esv(x).getRegObjRO()

class ExecutorVex : public Executor
{
public:
	ExecutorVex(InterpreterHandler *ie);
	virtual ~ExecutorVex(void);

	const llvm::Module * setModule(
		llvm::Module *module,
		const ModuleOptions &opts) { assert (0 == 1 && "BUSTED"); }

	void runFunctionAsMain(
		llvm::Function *f,
		int argc,
		char **argv,
		char **envp) { assert (0 == 1 && "LOL"); }

	virtual void runImage(void);

	Guest* getGuest(void) { return gs; }
	const Guest* getGuest(void) const { return gs; }

	MemoryManager* getMM(void) { return memory; }
	MemoryObject* allocRegCtx(ExecutionState* state, llvm::Function* f = 0);

	void setRegCtx(ExecutionState& state, MemoryObject* mo);
	void dumpSCRegs(const std::string& fname);

	virtual void printStackTrace(ExecutionState& st, std::ostream& o) const;
	virtual std::string getPrettyName(llvm::Function* f) const;
protected:
	virtual ExecutionState* setupInitialState(void);
	ExecutionState* setupInitialStateEntry(uint64_t entry_addr);

	void cleanupImage(void);

	virtual llvm::Function* getFuncByAddr(uint64_t addr);
  	virtual void executeInstruction(
		ExecutionState &state, KInstruction *ki);
	virtual void executeCallNonDecl(
		ExecutionState &state,
		KInstruction *ki,
		llvm::Function *f,
		std::vector< ref<Expr> > &arguments) { assert (0 == 1 && "STUB"); }
	virtual void instRet(ExecutionState &state, KInstruction *ki);
  	virtual void run(ExecutionState &initialState);

	virtual void callExternalFunction(
		ExecutionState &state,
		KInstruction *target,
		llvm::Function *function,
		std::vector< ref<Expr> > &arguments);

	virtual void handleXferSyscall(
		ExecutionState& state, KInstruction* ki);
	void handleXferJmp(
		ExecutionState& state, KInstruction* ki);
	virtual void handleXferCall(
		ExecutionState& state, KInstruction* ki);
	virtual void handleXferReturn(
		ExecutionState& state, KInstruction* ki);
	virtual void jumpToKFunc(ExecutionState& state, KFunction* kf);


	virtual void printStateErrorMessage(
		ExecutionState& state,
		const std::string& message,
		std::ostream& os);

	virtual void handleXfer(ExecutionState& state, KInstruction *ki);
	void updateGuestRegs(ExecutionState& s);

	uint64_t getStateStack(ExecutionState& s) const;
	ref<Expr> getRetArg(ExecutionState& state) const;
	ref<Expr> getCallArg(ExecutionState& state, unsigned int n) const;

	Guest		*gs;
	KModuleVex	*km_vex;
private:

	void markExit(ExecutionState& state, uint8_t);

	void bindMapping(
		ExecutionState* state,
		llvm::Function* f,
		GuestMem::Mapping m);
	void bindMappingPage(
		ExecutionState* state,
		llvm::Function* f,
		const GuestMem::Mapping& m,
		unsigned int pgnum);

	void prepState(ExecutionState* state, llvm::Function*);
	void installFDTInitializers(llvm::Function *init_func);
	void installFDTConfig(ExecutionState& state);
	void makeArgsSymbolic(ExecutionState* state);
	void makeMagicSymbolic(ExecutionState* state);
	void setupRegisterContext(ExecutionState* state, llvm::Function* f);
	void setupProcessMemory(ExecutionState* state, llvm::Function* f);

	void logXferRegisters(ExecutionState& state);

	void logSCRegs(ExecutionState& state);

	bool		dump_funcs;
	bool		in_sc;

	KFunction		*kf_scenter;
	SysModel		*sys_model;
	SyscallSFH		*sfh;
};

}
#endif

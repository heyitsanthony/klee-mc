#ifndef KLEE_EXECUTOR_VEX_H
#define KLEE_EXECUTOR_VEX_H

#include "collection.h"
#include "guest.h"
#include "guestcpustate.h"
#include "../../lib/Core/Executor.h"

#include <iostream>
#include <assert.h>

#include "Exempts.h"

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
class HostAccelerator;

#define es2esv(x)	static_cast<ExeStateVex&>(x)
#define es2esvc(x)	static_cast<const ExeStateVex&>(x)
#define GETREGOBJ(x)	es2esv(x).getRegObj()
#define GETREGOBJRO(x)	es2esvc(x).getRegObjRO()

#define AS_COPY2(s, z, x, y, w) (s).addressSpace.copyToBuf(\
	es2esvc((s)).getRegCtx(), z, offsetof(x,y), w)
#define AS_COPY(x,y,w)  AS_COPY2(state, &sysnr, x, y, w)
#define AS_COPYOUT(s, z, x, y, w) (s).addressSpace.copyOutBuf(\
	es2esv((s)).getRegCtx()->address + offsetof(x,y), (const char*)z, w)

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

	virtual void runImage(void) { runSym(NULL); }
	virtual void runSym(const char* symName);

	Guest* getGuest(void) { return gs; }
	const Guest* getGuest(void) const { return gs; }

	MemoryManager* getMM(void) { return memory; }
	MemoryObject* allocRegCtx(ExecutionState* state, llvm::Function* f = 0);

	void setRegCtx(ExecutionState& state, MemoryObject* mo);
	void dumpSCRegs(const std::string& fname);

	virtual void printStackTrace(
		const ExecutionState& st, std::ostream& o) const;
protected:
	virtual ExecutionState* setupInitialState(void);
	virtual ExecutionState* setupInitialStateEntry(uint64_t entry_addr);

	void cleanupImage(void);

	virtual llvm::Function* getFuncByAddr(uint64_t addr);
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

	virtual void handleXfer(ExecutionState& state, KInstruction *ki);

	uint64_t getStateStack(ExecutionState& s) const;
	ref<Expr> getRetArg(ExecutionState& state) const;
	ref<Expr> getCallArg(ExecutionState& state, unsigned int n) const;

	static void setKeepDeadStack(bool);

	Guest		*gs;
	KModuleVex	*km_vex;
private:
	unsigned getExitType(const ExecutionState& state) const;

	void markExit(ExecutionState& state, uint8_t);

	bool doAccel(ExecutionState& state, KInstruction* ki);

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
	llvm::Function* setupRuntimeFunctions(uint64_t entry_addr);
	void setupRegisterContext(ExecutionState* state, llvm::Function* f);
	void setupProcessMemory(ExecutionState* state, llvm::Function* f);

	bool		dump_funcs;
	bool		in_sc;

	KFunction		*kf_scenter;
	SysModel		*sys_model;

	llvm::Function		*img_init_func;
	uint64_t		img_init_func_addr;

	HostAccelerator		*hw_accel;
};

}
#endif

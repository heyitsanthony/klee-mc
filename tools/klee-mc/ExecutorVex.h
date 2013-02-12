#ifndef KLEE_EXECUTOR_VEX_H
#define KLEE_EXECUTOR_VEX_H

#include "collection.h"
#include "guest.h"
#include "guestcpustate.h"
#include "../../lib/Core/Executor.h"

#include <iostream>
#include <assert.h>

namespace llvm
{
class Function;
class GlobalVariable;
}

typedef std::pair<unsigned /* off */, unsigned /* len */> Exemptent;
typedef std::vector<Exemptent> Exempts;

/* inlined so that kmc-replay will work */
static inline Exempts getRegExempts(const Guest* gs)
{
	Exempts	ret;

	ret.push_back(
		Exemptent(
			gs->getCPUState()->getStackRegOff(),
			(gs->getMem()->is32Bit()) ? 4 : 8));

	switch (gs->getArch()) {
	case Arch::X86_64:
		ret.push_back(Exemptent(176 /* guest_DFLAG */, 8));
		ret.push_back(Exemptent(200 /* guest_FS_ZERO */, 8));
		break;
	case Arch::ARM:
		ret.push_back(Exemptent(380 /* TPIDRURO */, 4));
		break;
	default:
		assert (0 == 1 && "UNSUPPORTED ARCHITECTURE");
	}

	return ret;
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
#define es2esvc(x)	static_cast<const ExeStateVex&>(x)
#define GETREGOBJ(x)	es2esv(x).getRegObj()
#define GETREGOBJRO(x)	es2esvc(x).getRegObjRO()

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
	virtual std::string getPrettyName(const llvm::Function* f) const;
protected:
	virtual ExecutionState* setupInitialState(void);
	virtual ExecutionState* setupInitialStateEntry(uint64_t entry_addr);

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
	unsigned getExitType(const ExecutionState& state) const;

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
	llvm::Function* setupRuntimeFunctions(uint64_t entry_addr);
	void setupRegisterContext(ExecutionState* state, llvm::Function* f);
	void setupProcessMemory(ExecutionState* state, llvm::Function* f);

	void logXferRegisters(ExecutionState& state);

	void logSCRegs(ExecutionState& state);

	bool		dump_funcs;
	bool		in_sc;

	KFunction		*kf_scenter;
	SysModel		*sys_model;

	llvm::Function		*img_init_func;
	uint64_t		img_init_func_addr;
};

}
#endif

#ifndef KLEE_EXECUTOR_VEX_H
#define KLEE_EXECUTOR_VEX_H

#include "guest.h"
#include "../../lib/Core/Executor.h"

#include <hash_map>
#include <assert.h>

class VexXlate;
class VexSB;
class VexFCache;

namespace llvm
{
class Function;
class GlobalVariable;
}

namespace klee {  
class KModule;
class MemoryObject;
class ObjectState;

// ugh g++, you delicate garbage
typedef __gnu_cxx::hash_map<uintptr_t /* Func*/, VexSB*> func2vsb_map;

class ExecutorVex : public Executor
{
public:
	ExecutorVex(
		const InterpreterOptions &opts,
		InterpreterHandler *ie,
		Guest* gs);
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

protected:
  	virtual void executeInstruction(
		ExecutionState &state, KInstruction *ki);
	virtual void executeCallNonDecl(
		ExecutionState &state, 
		KInstruction *ki,
		llvm::Function *f,
		std::vector< ref<Expr> > &arguments) { assert (0 == 1 && "STUB"); }
	virtual void instRet(ExecutionState &state, KInstruction *ki);
  	virtual void run(ExecutionState &initialState);

	virtual const Cell& eval(
		KInstruction *ki,
		unsigned index,
		ExecutionState &state) const;

	virtual void callExternalFunction(
		ExecutionState &state,
		KInstruction *target,
		llvm::Function *function,
		std::vector< ref<Expr> > &arguments) { assert (0 == 1 && "STUB"); }

	virtual bool handleXferSyscall(
		ExecutionState& state, KInstruction* ki);
	void handleXferJmp(
		ExecutionState& state, KInstruction* ki);

	virtual void handleXfer(ExecutionState& state, KInstruction *ki);
	void updateGuestRegs(ExecutionState& s);
	void sc_ret_v(ExecutionState& state, uint64_t v);

	VexXlate	*xlate;
	Guest		*gs;
private:
	void markExitIgnore(ExecutionState& state);
	void makeRangeSymbolic(
		ExecutionState& state, void* addr, unsigned sz,
		const char* name = NULL);

	void makeSymbolicTail(
		ExecutionState& state,
		const MemoryObject* mo,
		unsigned taken,
		const char* name);
	void makeSymbolicHead(
		ExecutionState& state,
		const MemoryObject* mo,
		unsigned taken,
		const char* name);
	void makeSymbolicMiddle(
		ExecutionState& state,
		const MemoryObject* mo,
		unsigned mo_off,
		unsigned taken,
		const char* name);

	void bindMapping(
		ExecutionState* state,
		llvm::Function* f,
		GuestMem::Mapping m);
	void bindModuleConstants(void);
	void bindKFuncConstants(KFunction *kfunc);
	void bindModuleConstTable(void);
	void initializeGlobals(ExecutionState& state);
	void prepState(ExecutionState* state, llvm::Function*);
	void setupRegisterContext(ExecutionState* state, llvm::Function* f);
	void setupProcessMemory(ExecutionState* state, llvm::Function* f);
	llvm::Function* getFuncFromAddr(uint64_t addr);
	void allocGlobalVariableDecl(
		ExecutionState& state,
		const llvm::GlobalVariable& gv);
	void allocGlobalVariableNoDecl(
		ExecutionState& state,
		const llvm::GlobalVariable& gv);

	void dumpRegs(ExecutionState& s);

	void handleXferCall(
		ExecutionState& state, KInstruction* ki);
	void handleXferReturn(
		ExecutionState& state, KInstruction* ki);
	void jumpToKFunc(ExecutionState& state, KFunction* kf);

	ObjectState* sc_ret_ge0(ExecutionState& state);
	ObjectState* sc_ret_le0(ExecutionState& state);
	ObjectState* sc_ret_or(ExecutionState& state, uint64_t o1, uint64_t o2);
	ObjectState* sc_ret_range(
		ExecutionState& state, uint64_t lo, uint64_t hi);
	void sc_fail(ExecutionState& state);

	void sc_writev(ExecutionState& state);
	void sc_getcwd(ExecutionState& state);
	void sc_read(ExecutionState& state);
	void sc_mmap(ExecutionState& state, KInstruction* ki);
	void sc_munmap(ExecutionState& state);
	void sc_stat(ExecutionState& state);
	
	ObjectState* makeSCRegsSymbolic(ExecutionState& state);

	struct XferStateIter
	{
		ref<Expr>	v;
		ExecutionState* free;
		llvm::Function*	f;
		StatePair 	res;
		bool		first;
	};
	void xferIterInit(
		struct XferStateIter& iter,
		ExecutionState* state,
		KInstruction* ki);
	bool xferIterNext(struct XferStateIter& iter);

	func2vsb_map	func2vsb_table;
	VexFCache	*xlate_cache;
	MemoryObject	*state_regctx_mo;

	unsigned int	sc_dispatched;
	unsigned int	sc_retired;

	bool		dump_funcs;

	unsigned int	syscall_c[512];
};

}
#endif

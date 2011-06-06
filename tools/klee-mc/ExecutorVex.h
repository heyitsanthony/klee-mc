#ifndef KLEE_EXECUTOR_VEX_H
#define KLEE_EXECUTOR_VEX_H

#include "../../lib/Core/Executor.h"

#include <hash_map>
#include <assert.h>

class GuestState;
class VexXlate;
class VexSB;

namespace llvm
{
class Function;
}

namespace klee {  
class KModule;

class EmittedVexSB
{
public: 
	EmittedVexSB(VexSB* in_vsb, llvm::Function* in_f)
		: esb_vsb(in_vsb), esb_f(in_f) {}
	virtual ~EmittedVexSB();
	VexSB		*esb_vsb;
	llvm::Function	*esb_f;
};

// ugh g++, you delicate garbage
typedef __gnu_cxx::hash_map<uint64_t, EmittedVexSB*> vexsb_map;

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
		ExecutionState &state, KInstruction *ki);
	virtual void executeCallNonDecl(
		ExecutionState &state, 
		KInstruction *ki,
		Function *f,
		std::vector< ref<Expr> > &arguments) { assert (0 == 1 && "STUB"); }
	virtual void instRet(ExecutionState &state, KInstruction *ki);
  	virtual void run(ExecutionState &initialState);

	virtual const Cell& eval(
		KInstruction *ki,
		unsigned index,
		ExecutionState &state) const;

	virtual llvm::Function* getCalledFunction(
		llvm::CallSite &cs, ExecutionState &state) { 
			assert (0 == 1 && "STUB"); }

	virtual void callExternalFunction(
		ExecutionState &state,
		KInstruction *target,
		Function *function,
		std::vector< ref<Expr> > &arguments) { assert (0 == 1 && "STUB"); }


private:
	void bindModuleConstants(void);
	void prepArgs(ExecutionState* state, llvm::Function*);
	void setupRegisterContext(ExecutionState* state, llvm::Function* f);
	void setupProcessMemory(ExecutionState* state, llvm::Function* f);
	llvm::Function* getFuncFromAddr(uint64_t addr);
	EmittedVexSB* getEmitted(uint64_t addr);

	KModule		*kmodule;
	VexXlate	*xlate;
	GuestState	*gs;

	vexsb_map 	vsb_cache;
};

}
#endif

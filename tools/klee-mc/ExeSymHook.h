#ifndef EXESYMHOOK_H
#define EXESYMHOOK_H

#include "ExecutorVex.h"
#include "vexexec.h"

class GenLLVM;
class VexHelpers;
class Symbol;

namespace llvm
{
class Function;
}

namespace klee
{
class ESVSymHook;

class ExeSymHook : public ExecutorVex
{
public:
	ExeSymHook(InterpreterHandler *ie, Guest* gs);
	virtual ~ExeSymHook(void);

protected:
	virtual void executeCall(ExecutionState &state,
		KInstruction *ki,
		llvm::Function *f,
		std::vector< ref<Expr> > &arguments);
	virtual void jumpToKFunc(ExecutionState& state, KFunction* kf);
	virtual ExecutionState* setupInitialState(void);

private:
	void watchEnterXfer(ExecutionState& es, llvm::Function* f);
	void watchFunc(ExecutionState& es, llvm::Function* f);
	void unwatch(ESVSymHook &esh);
	void unwatchFree(ESVSymHook &esh);
	void unwatchMalloc(ESVSymHook &esh);
	llvm::Function	*f_malloc;
	llvm::Function	*f_free;
};

}

#endif

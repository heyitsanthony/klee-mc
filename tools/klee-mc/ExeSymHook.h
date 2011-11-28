#ifndef EXESYMHOOK_H
#define EXESYMHOOK_H

#include "ExecutorVex.h"
#include "vexexec.h"

class GenLLVM;
class VexHelpers;
class Symbol;
class Symbols;

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
	static ExeSymHook* create(InterpreterHandler *ie, Guest* gs);
	virtual ~ExeSymHook(void);

protected:
	ExeSymHook(InterpreterHandler *ie, Guest* gs);

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

	struct sym2func_t {
		const char	*sym_name;
		llvm::Function	**f;
	};
	void sym2func(const Symbols* syms, struct sym2func_t* stab);

	llvm::Function	*f_malloc, *f_int_malloc, *f_memalign;
	llvm::Function	*f_free;
	llvm::Function	*f_vasprintf, *f_asprintf;
};

}

#endif

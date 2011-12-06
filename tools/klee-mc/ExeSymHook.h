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
	bool isFreeFunc(llvm::Function* f) const;
	bool isMallocFunc(llvm::Function* f) const;
	bool isWatchable(llvm::Function* f) const;

	void watchEnterXfer(ExecutionState& es, llvm::Function* f);
	void watchFunc(ExecutionState& es, llvm::Function* f);
	void watchFuncArg(
		ExecutionState& es, llvm::Function* f, ref<Expr>& arg);
	void unwatch(ESVSymHook &esh);
	void unwatchFree(ESVSymHook &esh);
	void unwatchMalloc(ESVSymHook &esh);

	struct sym2func_t {
		const char	*sym_name;
		llvm::Function	**f;
	};
	void sym2func(const Symbols* syms, struct sym2func_t* stab);

	/*  If size is  0,  then malloc() returns either NULL, 
	 *  or a unique pointer value that can later be successfully
	 *  passed to free().
	 * oh libc, you so crazy */
	uint64_t	zero_malloc_ptr;

	#define FM_INT_MALLOC		0
	#define FM_MALLOC		1
	#define FM_MEMALIGN		2
	#define FM_GI_LIBC_MALLOC	3
	#define FM_GI_LIBC_REALLOC	4
	#define FM_REALLOC		5
	#define FM_CALLOC		6
	#define FM_CALLOC2		7
	#define FM_SIZE			8
	llvm::Function	*f_mallocs[FM_SIZE];

	#define FF_INT_FREE	0
	#define FF_FREE		1
	#define FF_GI_LIBC_FREE	2
	#define FF_SIZE		3
	llvm::Function	*f_frees[FF_SIZE];
};

}

#endif

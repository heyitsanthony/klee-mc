#include <iostream>
#include "klee/Internal/Module/KModule.h"
#include <llvm/Function.h>
#include "ExeSymHook.h"
#include "ESVSymHook.h"
#include "symbols.h"
#include "guest.h"


using namespace klee;

#define es2esh(x)	static_cast<ESVSymHook&>(x)

ExeSymHook::ExeSymHook(
	const InterpreterOptions &opts,
	InterpreterHandler *ie,
	Guest* gs)
: ExecutorVex(opts, ie, gs)
, f_malloc(NULL)
, f_free(NULL)
{
	ExeStateBuilder::replaceBuilder(new ESVSymHookBuilder());
}

ExeSymHook::~ExeSymHook(void) {}

void ExeSymHook::executeCall(
	ExecutionState &state,
	KInstruction *ki,
	llvm::Function *f,
	std::vector< ref<Expr> > &arguments)
{
	watchEnterXfer(state, f);
	ExecutorVex::executeCall(state, ki, f, arguments);
}

void ExeSymHook::watchEnterXfer(ExecutionState& es, llvm::Function* f)
{
	ESVSymHook	&esh(es2esh(es));

	if (esh.isWatched())
		return;
	watchFunc(es, f);
}

void ExeSymHook::jumpToKFunc(ExecutionState& state, KFunction* kf)
{
	ESVSymHook	&esh(es2esh(state));

	if (!esh.isWatched()) {
		watchEnterXfer(state, kf->function);
	} else {
		// if (esh.isWatched())
		/* check the water mark, if the stack is greater than
		 * the watermark (e.g. closer to the beginning of stack mem),
		 * then we have returned from the watched function */
		if (getStateStack(state) > esh.getWatermark())
			unwatch(esh);
	}

	ExecutorVex::jumpToKFunc(state, kf);
}

void ExeSymHook::unwatch(ESVSymHook &esh)
{
	llvm::Function		*watch_f;
	const ConstantExpr	*ret_ce;
	ref<Expr>		ret_arg;

	ret_arg = getRetArg(esh);
	ret_ce = dyn_cast<ConstantExpr>(ret_arg);

	if (ret_ce == NULL)
		goto done;

	watch_f = esh.getWatchedFunc();
	if (watch_f == f_malloc) {
		heap_ptrs.insert(ret_ce->getZExtValue());
	} else if (watch_f == f_free) {
		if (heap_ptrs.count(ret_ce->getZExtValue()) == 0) {
			terminateStateOnError(
			esh,
			"heap error: freeing non-malloced pointer",
			"heapfree.err");
		}
		heap_ptrs.erase(ret_ce->getZExtValue());
	} else {
		assert (0 == 1 && "WTF");
	}

done:
#if 0
	std::cerr << "\nUNWATCHED: param=";
	esh.getWatchParam()->print(std::cerr);
	std::cerr << "\nRETVAL=";
	ret_arg->print(std::cerr);
	std::cerr << "\n";
#endif
	esh.unwatch();
}

void ExeSymHook::watchFunc(ExecutionState& es, llvm::Function* f)
{
	ESVSymHook		&esh(es2esh(es));
	uint64_t		stack_pos;

	if (f != f_malloc && f != f_free)
		return;

	stack_pos = getStateStack(es);
	if (!stack_pos)
		return;

	std::cerr << "WATCHING: " << f->getNameStr() << "\n";
	esh.enterWatchedFunc(f, getCallArg(es, 0), stack_pos);
}

ExecutionState* ExeSymHook::setupInitialState(void)
{
	ExecutionState	*ret;
	Guest		*gs;
	const Symbols	*syms;
	const Symbol	*sym_malloc, *sym_free;


	ret = ExecutorVex::setupInitialState();

	gs = getGuest();
	syms = gs->getDynSymbols();
	assert (syms != NULL && "Can't hook without symbol names");

	sym_malloc = syms->findSym("malloc");
	sym_free = syms->findSym("free");

	assert (sym_malloc && sym_free && "Could not finds syms to hook");

	f_malloc = getFuncByAddr(sym_malloc->getBaseAddr());
	f_free = getFuncByAddr(sym_free->getBaseAddr());
	std::cerr << "MALLOC = " << f_malloc->getNameStr() << '\n';

	assert (f_malloc && f_free && "Could not decode hooked funcs");

	return ret;
}
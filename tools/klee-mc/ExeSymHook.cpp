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

	if (esh.isWatched()) {
		std::cerr << "WUUUUUT\n";
		esh.incWatchDepth();
		return;
	}
	std::cerr << "GOT: " << f->getNameStr() << "\n";
	watchFunc(es, f);
}

void ExeSymHook::jumpToKFunc(ExecutionState& state, KFunction* kf)
{
	watchEnterXfer(state, kf->function);
	ExecutorVex::jumpToKFunc(state, kf);
}

void ExeSymHook::watchFunc(ExecutionState& es, llvm::Function* f)
{
	ESVSymHook	&esh(es2esh(es));

	if (f == f_malloc) {
		std::cerr << "HELLOOOOOO MALLOC\n";
		esh.setWatchedFunc(f_malloc, getCallArg(es, 0));
		return;
	}

	if (f == f_free) {
		esh.setWatchedFunc(f_free, getCallArg(es, 0));
		return;
	}
}

void ExeSymHook::handleXferReturn(ExecutionState& state, KInstruction* ki)
{
	ESVSymHook	&esh(es2esh(state));

	if (esh.isWatched() && esh.decWatchDepth() == 0)
		unwatchFunc(esh);

	ExecutorVex::handleXferReturn(state, ki);
}

void ExeSymHook::unwatchFunc(ESVSymHook& esh)
{
	esh.getWatchParam()->print(std::cerr);
	esh.unwatch();
}

ExecutionState* ExeSymHook::setupInitialState(void)
{
	ExecutionState	*ret;
	Guest		*gs;
	const Symbols	*syms;
	const Symbol	*sym_malloc, *sym_free;


	ret = ExecutorVex::setupInitialState();

	gs = getGuest();
	syms = gs->getSymbols();
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
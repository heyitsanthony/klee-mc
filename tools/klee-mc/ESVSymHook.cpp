#include <iostream>
#include "ESVSymHook.h"

using namespace klee;

ESVSymHook::ESVSymHook(const ESVSymHook& src)
: ExeStateVex(src)
, cur_watched_f(src.cur_watched_f)
, param(src.param)
, watched_depth(0)
{}

void ESVSymHook::setWatchedFunc(llvm::Function* f, ref<Expr> in_param)
{
	cur_watched_f = f;
	watched_depth = 1;
	param = in_param;
}

void ESVSymHook::bindObject(const MemoryObject *mo, ObjectState *os)
{
	if (isWatched()) {
		std::cerr << "MADE IT IN HEAP\n";
	}
	ExecutionState::bindObject(mo, os);
}

void ESVSymHook::unbindObject(const MemoryObject* mo)
{
	if (isWatched()) {
		std::cerr << "FREEING FROM HEAP\n";
	}
	ExecutionState::unbindObject(mo);
}


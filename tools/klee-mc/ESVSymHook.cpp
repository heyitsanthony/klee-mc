#include <iostream>
#include "ESVSymHook.h"

using namespace klee;

ESVSymHook::ESVSymHook(const ESVSymHook& src)
: ExeStateVex(src)
, cur_watched_f(src.cur_watched_f)
, param(src.param)
{}

void ESVSymHook::enterWatchedFunc(
	llvm::Function* f, ref<Expr> in_param, uint64_t stack_watermark)
{
	cur_watched_f = f;
	param = in_param;
	enter_stack_watermark = stack_watermark;
}

void ESVSymHook::addHeapPtr(uint64_t x)
{
	heap_set = heap_set.insert(x);
}

void ESVSymHook::rmvHeapPtr(uint64_t x)
{
	heap_set = heap_set.remove(x);
}

bool ESVSymHook::hasHeapPtr(uint64_t x) const
{
	return (heap_set.count(x) != 0);
}

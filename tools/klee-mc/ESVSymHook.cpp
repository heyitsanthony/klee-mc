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

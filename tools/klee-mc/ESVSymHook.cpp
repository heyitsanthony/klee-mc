#include <iostream>
#include "static/Sugar.h"
#include "ESVSymHook.h"

using namespace klee;

ESVSymHook::ESVSymHook(const ESVSymHook& src)
: ExeStateVex(src)
, heap_map(src.heap_map)
, blessed_mo(src.blessed_mo)
, cur_watched_f(src.cur_watched_f)
, param(src.param)
, enter_stack_watermark(src.enter_stack_watermark)
{
}

void ESVSymHook::enterWatchedFunc(
	llvm::Function* f, ref<Expr> in_param, uint64_t stack_watermark)
{
	cur_watched_f = f;
	param = in_param;
	enter_stack_watermark = stack_watermark;
}

void ESVSymHook::addHeapPtr(uint64_t x, unsigned int len)
{
	heap_map = heap_map.insert(std::make_pair(x,len));
	//std::cerr << "adding heap ptr " << (void*)x
	//	<< "--" << (void*)(x+len) << std::endl;
}

void ESVSymHook::rmvHeapPtr(uint64_t x)
{
	//std::cerr << "rmv heap ptr " << (void*)x  << '\n';
	heap_map = heap_map.remove(x);
}

/* check if 'x' was returned by malloc(), but not freed */
bool ESVSymHook::hasHeapPtr(uint64_t x) const
{
	return (heap_map.count(x) != 0);
}

/* check whether x--(x+len-1) is contained within the heap */
bool ESVSymHook::heapContains(uint64_t x, unsigned int len) const
{
	heap_map_ty::iterator	it(heap_map.lower_bound(x));
	uint64_t		found_base;
	unsigned int		found_len;

	if (it == heap_map.end()) {
		return false;
	}

	/* it should have the first element that is <= x */
	found_base = it->first;
	found_len = it->second;

	/* too low?  (0xa + 1 <= 0xb) ==> error*/
	if ((found_base + found_len) <= x)
		return false;

	/* too long? */
	if ((x + len) > (found_base + found_len))
		return false;

	/* fits like a glove */
	return true;
}

bool ESVSymHook::isBlessed(const MemoryObject* mo) const
{
	if (blessed_mo.count(mo))
		return true;

	return false;
}

void ESVSymHook::bindObject(const MemoryObject *mo, ObjectState *os)
{
	/* bless small objects so that alloca's work OK */
	if (isWatched() == false || mo->size < 2048)
		blessed_mo = blessed_mo.insert(mo);

	ExeStateVex::bindObject(mo, os);
}

void ESVSymHook::unbindObject(const MemoryObject* mo)
{
	if (isBlessed(mo))
		blessed_mo = blessed_mo.remove(mo);

	ExeStateVex::unbindObject(mo);
}

/* mark entire address space as blessed */
void ESVSymHook::blessAddressSpace(void)
{
	foreach (it, addressSpace.begin(), addressSpace.end()) {
		blessed_mo = blessed_mo.insert(it->first);
	}
}

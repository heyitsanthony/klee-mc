#include "CoreStats.h"
#include "Memory.h"
#include "MemoryManager.h"
#include "klee/Common.h"
#include "klee/ExecutionState.h"
#include <iostream>

using namespace klee;

#define MAX_ALLOC_BYTES	(16*1024*1024)


MemoryManager::MemoryManager(void)
{
	MemoryObject::memoryManager = this;
}

MemoryManager::~MemoryManager(void)
{
	anonMemObjs.clear();
	if (objects.size() != 0) {
		std::cerr
			<< "MemoryManager leaking " << objects.size()
			<< " objects. (mallocKey.isFixed?)\n";
	}
	MemoryObject::memoryManager = NULL;
}

bool MemoryManager::isGoodSize(uint64_t size) const
{
	if (size <= MAX_ALLOC_BYTES) return true;

	klee_warning_once(0, "failing large alloc: %u bytes", (unsigned) size);
	return false;
}

MemoryObject* MemoryManager::allocateAt(
	ExecutionState& state,
	uint64_t address, uint64_t size,
	const llvm::Value *allocSite)
{
	MemoryObject *res;
	assert (size != 0);

	MallocKey mallocKey(allocSite,
		      state.mallocIterations[allocSite]++,
		      size, false, true, false);

	++stats::allocations;
	res = new MemoryObject(address, size, mallocKey);

	state.memObjects.push_back(ref<MemoryObject>(res));
	objects.insert(res);
	return res;
}

MemoryObject *MemoryManager::allocateFixed(
	uint64_t address, uint64_t size,
	const llvm::Value *allocSite,
	ExecutionState *state)
{
	assert((state || allocSite) && "Insufficient MallocKey data");

	MallocKey mallocKey(
		allocSite,
		state ? state->mallocIterations[allocSite]++ : 0,
		size, false, true, true);

	++stats::allocations;
	MemoryObject *res = new MemoryObject(address, size, mallocKey);

	if (state)
		state->memObjects.push_back(ref<MemoryObject>(res));
	else
		anonMemObjs.push_back(ref<MemoryObject>(res));

	if(size)
		objects.insert(res);

	return res;
}

#include "CoreStats.h"
#include "Memory.h"
#include "MemoryManager.h"
#include "HeapMM.h"
#include "DeterministicMM.h"
#include "klee/Common.h"
#include "klee/ExecutionState.h"
#include <llvm/Support/CommandLine.h>
#include <iostream>

using namespace klee;
using namespace llvm;

enum MMType { MM_HEAP, MM_DETERMINISTIC };

bool MemoryManager::m_is32Bit = false;

namespace
{
	cl::opt<MMType>
	UseMM(
	"mm-type",
	cl::desc("Set the type of memory manager used"),
	cl::values(
		clEnumValN(MM_HEAP, "heap", "Heap based MM"),
		clEnumValN(
			MM_DETERMINISTIC, "deterministic", "Deterministic MM"),
		clEnumValEnd),
	cl::init(MM_HEAP));

	cl::opt<uint64_t>
	LimitMaxAlloc(
		"mm-max-alloc",
		cl::desc("Set maximum contiguous allocation in bytes"),
		cl::init(512*1024*1024));
}

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
	if (!LimitMaxAlloc || size <= LimitMaxAlloc) return true;

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

	if (state.addressSpace.resolveOneMO(address) != NULL) {
		std::cerr << "[MemoryManager] ADDRESS ALREADY TAKEN!?\n";
		return NULL;
	}

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

MemoryManager* MemoryManager::create(void)
{
	switch (UseMM) {
	case MM_DETERMINISTIC:
		return new DeterministicMM();
	case MM_HEAP:
		return new HeapMM();
	default:
		return NULL;
	}
}

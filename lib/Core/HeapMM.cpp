//===-- HeapMM.cpp -------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CoreStats.h"
#include "Memory.h"
#include "HeapMM.h"

#include "static/Sugar.h"
#include "klee/Common.h"
#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Solver.h"

#include "llvm/Support/CommandLine.h"

using namespace klee;

HeapMM::HeapMM(void)
{
	HeapObject::memoryManager = this;
}

HeapMM::~HeapMM()
{
	anonHeapObjs.clear();
	HeapObject::memoryManager = NULL;
}

MemoryObject *HeapMM::allocate(
	uint64_t size, bool isLocal,
	bool isGlobal,
	const llvm::Value *allocSite,
	ExecutionState *state)
{
	if (!isGoodSize(size)) return NULL;

	assert((state || allocSite) && "Insufficient MallocKey data");
	MallocKey mallocKey(allocSite,
		state ? state->mallocIterations[allocSite]++ : 0,
		size, isLocal, isGlobal, false);
	HeapObject *heapObj = NULL;

	// find smallest suitable existing heap object to satisfy request
	// suitable is defined as:
	//  1) same allocSite
	//  2) same malloc iteration number (at allocSite) within path
	//  3) size <= size being requested
	if (state && size) {
		std::pair<HeapMap::iterator,HeapMap::iterator> hitPair;

		hitPair = heapObjects.equal_range(mallocKey);
		foreach (hit, hitPair.first, hitPair.second) {
			HeapObject *curObj = hit->second;
			// MallocKey sigh = hit->first;

			// we should never have more than
			// one candidate object with the same size
			assert(!heapObj || heapObj->size != curObj->size);

			if (	size <= curObj->size &&
				(!heapObj || curObj->size < heapObj->size))
			{
				heapObj = curObj;
			}
		}
	}

	// No match; allocate new heap object
	if (heapObj == NULL && size) {
		assert (
		(!state || state->isReplay || state->isReplayDone()) &&
		"on reconstitution, existing heap object must be chosen");

		// if we used to have an object with this mallocKey, reuse the same size
		// so we can get hits in the cache and RWset
		MallocKey::seensizes_ty::iterator it;
		if (	allocSite &&
			(	it = MallocKey::seenSizes.find(mallocKey)) !=
				MallocKey::seenSizes.end())
		{
			std::set<uint64_t> &sizes(it->second);
			std::set<uint64_t>::iterator it2;

			it2 = sizes.lower_bound(size);
			if(it2 != sizes.end() && *it2 > size)
			mallocKey.size = *it2;
		}

		heapObj = new HeapObject(size);
		++stats::allocations;

		if (allocSite)
			MallocKey::seenSizes[mallocKey].insert(mallocKey.size);

		heapObjects.insert(std::make_pair(mallocKey, heapObj));
	}

	bool anonymous = (state == NULL);
	MemoryObject *res;

	res = new MemoryObject(size ? heapObj->address : 0, size, mallocKey);

	// if not replaying state, add reference to heap object
	if (state && (state->isReplay || state->isReplayDone()))
		anonymous |= !state->pushHeapRef(heapObj);

	// add reference to memory object
	if (state)
		state->memObjects.push_back(ref<MemoryObject>(res));
	else
		anonMemObjs.push_back(ref<MemoryObject>(res));

	// heap object anonymously allocated; keep references to such objects so
	// they're freed on exit. These include globals, etc.
	if(anonymous)
		anonHeapObjs.push_back(ref<HeapObject>(heapObj));

	if(size)
		objects.insert(res);

	return res;
}

std::vector<MemoryObject*> HeapMM::allocateAlignedChopped(
	uint64_t size,
	unsigned pow2,
	const llvm::Value *allocSite,
	ExecutionState *state)
{
	std::vector<HeapObject*>	heapObjs;
	std::vector<MemoryObject*>	ret;

	assert (pow2 == 12 && "Expecting a 4KB page");
	if (!isGoodSize(size)) return ret;
	if (size == 0) return ret;

	assert(state  && "Insufficient MallocKey data");
	MallocKey mallocKey(
		allocSite,
		state->mallocIterations[allocSite]++,
		4096,
		true,
		false,
		false);

	heapObjs = HeapObject::contiguousPages(size);
	if (heapObjs.size() == 0)
		return ret;

	++stats::allocations;

	if (allocSite) MallocKey::seenSizes[mallocKey].insert(mallocKey.size);

	for (unsigned i = 0; i < heapObjs.size(); i++) {
		MemoryObject	*mo;
		mo = insertHeapObj(state, heapObjs[i], mallocKey, 4096);
		ret.push_back(mo);
	}

	return ret;
}

MemoryObject* HeapMM::insertHeapObj(
	ExecutionState* state,
	HeapObject* heapObj,
	MallocKey& mk,
	unsigned size)
{
	MemoryObject	*cur_mo;
	bool		anonymous;

	anonymous = (state == NULL);

	heapObjects.insert(std::make_pair(mk, heapObj));
	cur_mo = new MemoryObject(heapObj->address, size, mk);

	// if not replaying state, add reference to heap object
	if (state->isReplay || state->isReplayDone())
		anonymous |= !state->pushHeapRef(heapObj);

	// add reference to memory object
	state->memObjects.push_back(ref<MemoryObject>(cur_mo));

	// heap object anonymously allocated;
	// keep references to such objects so
	// they're freed on exit. These include globals, etc.
	if (anonymous)
		anonHeapObjs.push_back(ref<HeapObject>(heapObj));

	objects.insert(cur_mo);

	return cur_mo;
}

MemoryObject *HeapMM::allocateAligned(
	uint64_t size,
	unsigned pow2,
	const llvm::Value *allocSite,
	ExecutionState *state)
{
	HeapObject *heapObj;

	if (!isGoodSize(size)) return NULL;
	if (size == 0) return NULL;

	assert(state  && "Insufficient MallocKey data");
	MallocKey mallocKey(
		allocSite,
		state->mallocIterations[allocSite]++,
		size, true, false, false);

	heapObj = new HeapObject(size, 12);
	++stats::allocations;

	if (allocSite) MallocKey::seenSizes[mallocKey].insert(mallocKey.size);

	return insertHeapObj(state, heapObj, mallocKey, size);
}

void HeapMM::dropHeapObj(HeapObject* ho)
{
	// remove from heapObjects multimap
	foreach (hit,
		heapObjects.begin(),
		heapObjects.end())
	{
		HeapObject *heapObj = hit->second;
		if(heapObj == ho) {
			heapObjects.erase(hit);
			break;
		}
	}
}

//===-- MemoryManager.cpp -------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CoreStats.h"
#include "Memory.h"
#include "MemoryManager.h"

#include "static/Sugar.h"
#include "klee/Common.h"
#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Solver.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Streams.h"

using namespace klee;

/***/

MemoryManager::~MemoryManager() { 
  HeapObject::memoryManager = NULL;
  MemoryObject::memoryManager = NULL;
}

#define MAX_ALLOC_BYTES	(10*1024*1024)

MemoryObject *MemoryManager::allocate(
  uint64_t size, bool isLocal, 
  bool isGlobal,
  const llvm::Value *allocSite,
  ExecutionState *state)
{
  if (size > MAX_ALLOC_BYTES) {
    klee_warning_once(0, "failing large alloc: %u bytes", (unsigned) size);
    return 0;
  }

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
    std::pair<HeapMap::iterator,HeapMap::iterator> hitPair = 
      heapObjects.equal_range(mallocKey);
    foreach (hit, hitPair.first, hitPair.second) {
      HeapObject *curObj = hit->second;
      MallocKey sigh = hit->first;

      // we should never have more than one candidate object with the same size
      assert(!heapObj || heapObj->size != curObj->size);

      if (size <= curObj->size
          && (!heapObj || curObj->size < heapObj->size))
        heapObj = curObj;
    }
  }

  // No match; allocate new heap object
  if (!heapObj && size) {
    assert((!state
      || state->isReplay
      || state->replayBranchIterator == state->branchDecisionsSequence.end())
      &&
      "on reconstitution, existing heap object must be chosen");
    // if we used to have an object with this mallocKey, reuse the same size
    // so we can get hits in the cache and RWset
    MallocKey::seensizes_ty::iterator it;
    if (allocSite &&
        (it = MallocKey::seenSizes.find(mallocKey)) !=
          MallocKey::seenSizes.end()) {
      std::set<uint64_t> &sizes = it->second;
      std::set<uint64_t>::iterator it2 = sizes.lower_bound(size);
      if(it2 != sizes.end() && *it2 > size)
        mallocKey.size = *it2;
    }

    heapObj = new HeapObject(size);
    ++stats::allocations;

    if(allocSite)
      MallocKey::seenSizes[mallocKey].insert(mallocKey.size);

    heapObjects.insert(std::make_pair(mallocKey, heapObj));
  }

  bool anonymous = !state;
  MemoryObject *res;

  res = new MemoryObject(
    size ? heapObj->address : 0, size, mallocKey, heapObj);

  // if not replaying state, add reference to heap object
  if (state && (state->isReplay
      || state->replayBranchIterator == state->branchDecisionsSequence.end()))
    anonymous |= !state->branchDecisionsSequence.push_heap_ref(heapObj);

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

MemoryObject *MemoryManager::allocateFixed(
	uint64_t address, uint64_t size,
	const llvm::Value *allocSite,
	ExecutionState *state)
{
#ifndef NDEBUG
  foreach (it, objects.begin(), objects.end()) {
    MemoryObject *mo = *it;
    assert(!(address+size > mo->address && address < mo->address+mo->size) &&
           "allocated an overlapping object");
  }
#endif

  assert((state || allocSite) && "Insufficient MallocKey data");
  MallocKey mallocKey(allocSite,
                      state ? state->mallocIterations[allocSite]++ : 0,
                      size, false, true, true);

  ++stats::allocations;
  MemoryObject *res = new MemoryObject(address, size, mallocKey);

  anonMemObjs.push_back(ref<MemoryObject>(res));

  if(size)
    objects.insert(res);
  return res;
}

void MemoryManager::deallocate(const MemoryObject *mo)
{
  objects.erase(std::find(objects.begin(), objects.end(), mo));
  // delete the MemoryObjects, but don't free the underlying heap storage;
  // we have heap object reference counting for that
  delete mo;
}

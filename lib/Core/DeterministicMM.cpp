#include "CoreStats.h"
#include "klee/ExecutionState.h"
#include "DeterministicMM.h"

using namespace klee;

/** XXX update for 32-bit archs */
#define ANON_ADDR	(((uint64_t)1) << 41)
#define MAX_ADDR	(((uint64_t)1) << 40)
#define MIN_ADDR	0xa000000


DeterministicMM::DeterministicMM(void)
: anon_base(ANON_ADDR)
{}

DeterministicMM::~DeterministicMM(void)
{}

MemoryObject *DeterministicMM::allocate(
	uint64_t size, bool isLocal, bool isGlobal,
	const llvm::Value *allocSite, ExecutionState *state)
{
	MemoryObject	*res;
	uint64_t	new_addr;

	assert (size);

	if (!isGoodSize(size)) return NULL;

	assert((state || allocSite) && "Insufficient MallocKey data");
	MallocKey mallocKey(allocSite,
		state ? state->mallocIterations[allocSite]++ : 0,
		size, isLocal, isGlobal, false);

	// find smallest suitable existing heap object to satisfy request
	// suitable is defined as:
	//  1) same allocSite
	//  2) same malloc iteration number (at allocSite) within path
	//  3) size <= size being requested
	if (state) {
		new_addr = findFree(state, size);
	} else {
		new_addr = anon_base;
		anon_base += size;
	}

	assert (new_addr != 0);
	res = new MemoryObject(new_addr, size, mallocKey);
	++stats::allocations;

	// add reference to memory object
	if (state)
		state->memObjects.push_back(ref<MemoryObject>(res));
	else
		anonMemObjs.push_back(ref<MemoryObject>(res));

	objects.insert(res);

	return res;

}

MemoryObject *DeterministicMM::allocateAligned(
	uint64_t size, unsigned pow2,
	const llvm::Value *allocSite, ExecutionState *state)
{
	MemoryObject	*new_mo;
	uint64_t	new_addr;

	if (!isGoodSize(size)) return NULL;
	if (size == 0) return NULL;

	assert(state  && "Insufficient MallocKey data");
	MallocKey mallocKey(
		allocSite,
		state->mallocIterations[allocSite]++,
		size, true, false, false);

	++stats::allocations;

	if (allocSite) MallocKey::seenSizes[mallocKey].insert(mallocKey.size);

	new_addr = findFree(state, size);
	new_mo = insertNewMO(state, new_addr, mallocKey);
	return new_mo;
}

MemoryObject* DeterministicMM::insertNewMO(
	ExecutionState* state, uint64_t new_addr, MallocKey& mk)
{
	MemoryObject *new_mo;

	new_mo = new MemoryObject(new_addr, mk.size, mk);
	state->memObjects.push_back(ref<MemoryObject>(new_mo));
	objects.insert(new_mo);

	return new_mo;
}

std::vector<MemoryObject*> DeterministicMM::allocateAlignedChopped(
	uint64_t size, unsigned pow2,
	const llvm::Value *allocSite, ExecutionState *state)
{
	uint64_t			new_addr;
	std::vector<MemoryObject*>	ret;

	assert (pow2 == 12 && "Expecting a 4KB page");
	if (!isGoodSize(size)) return ret;
	if (size == 0) return ret;

	assert(state  && "Insufficient MallocKey data");
	MallocKey mallocKey(
		allocSite,
		state->mallocIterations[allocSite]++,
		size,
		true,
		false,
		false);
	if (allocSite) MallocKey::seenSizes[mallocKey].insert(mallocKey.size);
	++stats::allocations;

	new_addr = findFree(state, size, pow2);
	for (unsigned i = 0; i < (size / (1 << pow2)); i++) {
		MemoryObject	*mo;
		MallocKey	mk(
			allocSite,
			state->mallocIterations[allocSite],
			((uint64_t)1) << pow2, true, false, false);
		mo = insertNewMO(state, new_addr + (i << pow2), mk);
		ret.push_back(mo);
	}

	return ret;
}

uint64_t DeterministicMM::findFree(
	ExecutionState* state, uint64_t sz, uint64_t align_pow)
{
	const MemoryObject	*last_bound;
	uint64_t		first_addr;

	last_bound = state->addressSpace.getLastBoundHint();
	if (last_bound != NULL) {
		// XXX align on size?
		first_addr = last_bound->address + last_bound->size;
	} else {
		first_addr = MIN_ADDR+((state->getNumAllocs())*sz);
	}

	do {
		const MemoryObject	*mo;
		uint64_t		target_end;

		/* hm.. out of bounds.. start scan from scratch */
		if (first_addr >= MAX_ADDR)
			first_addr = MIN_ADDR;

		if (align_pow && (first_addr & ((1 << align_pow)-1)) != 0) {
			first_addr += (1 << align_pow);
			first_addr &= ~((((uint64_t)1) << align_pow)-1);
		}

		// want range [first_addr, target_end)
		target_end = first_addr + sz;

		// return first MO that is either equal or greater.
		MMIter mi_begin(state->addressSpace.lower_bound(first_addr));

		// nothing equal or greater-- the space is ours
		if (mi_begin == state->addressSpace.end()) {
			break;
		}

		// equal or greater...
		mo = mi_begin->first;
		assert (mo->address >= first_addr);

		assert (mo != NULL);
		if (target_end <= mo->address) {
			/* no overlap on [first_addr, target_end)-- this
			 * object comes some time after */
			mo = state->addressSpace.resolveOneMO(first_addr);
			if (mo == NULL)
				break;
		}

		/* end address overlaps with MO, bump and try again */
		first_addr = mo->address + mo->size;
	} while (1);

	assert (state->addressSpace.resolveOneMO(first_addr) == NULL);

	return first_addr;
}

#ifndef HEAPMM_H
#define HEAPMM_H

#include "MemoryManager.h"

namespace llvm
{
class Value;
}

namespace klee
{
class HeapMM : public MemoryManager
{
public:
	typedef std::multimap<MallocKey,HeapObject*> HeapMap;
	friend class HeapObject;

	HeapMM();
	virtual ~HeapMM(void);

	virtual MemoryObject *allocate(
		uint64_t size, bool isLocal, bool isGlobal,
		const llvm::Value *allocSite, ExecutionState *state);

	virtual MemoryObject *allocateAligned(
		uint64_t size, unsigned pow2,
		const llvm::Value *allocSite, ExecutionState *state);

	virtual std::vector<MemoryObject*> allocateAlignedChopped(
		uint64_t size, unsigned pow2,
		const llvm::Value *allocSite, ExecutionState *state);
protected:
	// references to anonymous HeapObjects (ones not tied to a state)
	std::list<ref<HeapObject> > anonHeapObjs;
	// map containing all allocated heap objects
	HeapMap heapObjects;

private:
	void dropHeapObj(HeapObject* ho);
	MemoryObject* insertHeapObj(
		ExecutionState* s,
		HeapObject* ho,
		MallocKey& mk,
		unsigned sz);
};
}
#endif

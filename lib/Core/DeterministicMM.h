#ifndef DETERMINISTICMM_H
#define DETERMINISTICMM_H

#include "MemoryManager.h"

namespace llvm
{
class Value;
}

namespace klee
{
class DeterministicMM : public MemoryManager
{
public:
	DeterministicMM();
	virtual ~DeterministicMM(void);

	virtual MemoryObject *allocate(
		uint64_t size, bool isLocal, bool isGlobal,
		const llvm::Value *allocSite, ExecutionState *state);

	virtual MemoryObject *allocateAligned(
		uint64_t size, unsigned pow2,
		const llvm::Value *allocSite, ExecutionState *state);

	virtual std::vector<MemoryObject*> allocateAlignedChopped(
		uint64_t size, unsigned pow2,
		const llvm::Value *allocSite, ExecutionState *state);

private:
	MemoryObject* insertNewMO(
		ExecutionState*, uint64_t addr, MallocKey& mk);

	uint64_t findFree(ExecutionState* state, uint64_t sz,
		uint64_t align_pow = 0);

	uint64_t anon_base;
};
}
#endif

//===-- MemoryManager.h -----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_MEMORYMANAGER_H
#define KLEE_MEMORYMANAGER_H

#include "Memory.h"

#include "klee/util/Ref.h"

#include <vector>
#include <map>
#include <set>
#include <list>
#include <stdint.h>

namespace llvm {
  class Value;
}

namespace klee
{
	class MemoryObject;
	class MemoryManager;
	class ExecutionState;
	class MallocKey;

class MemoryManager
{
	friend class MemoryObject;

public:
	static MemoryManager* create(void);
	virtual ~MemoryManager();

	virtual MemoryObject *allocate(
		uint64_t size, bool isLocal, bool isGlobal,
		const llvm::Value *allocSite, ExecutionState *state) = 0;

	virtual MemoryObject *allocateAligned(
		uint64_t size, unsigned pow2,
		const llvm::Value *allocSite, ExecutionState *state) = 0;

	virtual std::vector<MemoryObject*> allocateAlignedChopped(
		uint64_t size, unsigned pow2,
		const llvm::Value *allocSite, ExecutionState *state) = 0;

	virtual MemoryObject *allocateFixed(
		uint64_t address, uint64_t size,
		const llvm::Value *allocSite,
		ExecutionState *state);

	virtual MemoryObject *allocateAt(
		ExecutionState& state,
		uint64_t address, uint64_t size,
		const llvm::Value *allocSite);

	static void set32Bit(void) { m_is32Bit = true; }
	static bool is32Bit(void) { return m_is32Bit; }
protected:
	MemoryManager();

	typedef std::set<MemoryObject*> objects_ty;
	// pointers to all allocated MemoryObjects
	objects_ty objects;
	// references to anonymous MemoryObjects (ones not tied to a state)
	std::list<ref<MemoryObject> > anonMemObjs;

	bool isGoodSize(uint64_t) const;
private:
	static bool	m_is32Bit;
};

} // End klee namespace

#endif

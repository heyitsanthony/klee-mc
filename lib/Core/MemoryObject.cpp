#include "Memory.h"
#include "MemoryManager.h"

#include "Context.h"
#include "klee/Common.h"
#include "klee/util/BitArray.h"

#include "ObjectHolder.h"

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Value.h>
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <iostream>
#include <cassert>
#include <sstream>

using namespace llvm;
using namespace klee;

MemoryManager* MemoryObject::memoryManager = NULL;

unsigned MemoryObject::counter = 0;
unsigned MemoryObject::numMemObjs = 0;


MemoryObject::MemoryObject(uint64_t _address)
: id(counter++)
, size(0)
, address(_address)
, mallocKey(0, 0, 0, false, false, true)
, refCount(0)
{
	numMemObjs++;
}

MemoryObject::MemoryObject(
  uint64_t _address,
  unsigned _size,
  const MallocKey &_mallocKey)
: id(counter++)
, size(_size)
, address(_address)
, name("unnamed")
, mallocKey(_mallocKey)
, fake_object(false)
, isUserSpecified(false)
, refCount(_mallocKey.isFixed ? 1 /* immortal */ : 0)
{
	numMemObjs++;
}

void MemoryObject::remove()
{
	assert(memoryManager);
	memoryManager->objects.erase(this);
}

MemoryObject::~MemoryObject(void)
{
	if(size && memoryManager)
		remove();
	numMemObjs--;
}

void MemoryObject::getAllocInfo(std::string &result) const
{
	llvm::raw_string_ostream info(result);

	info << "MO" << id << "[" << size << "]";

	/* this code kind of makes me nauseous */
	if (!mallocKey.allocSite) {
		info << " (no allocation info)";
		info.flush();
		return;
	}

	info << " allocated at ";
	if (const Instruction *i = dyn_cast<Instruction>(mallocKey.allocSite)) {
		info << i->getParent()->getParent()->getName().str() << "():";
		info << *i;
	} else if (const GlobalValue *gv =
	       dyn_cast<GlobalValue>(mallocKey.allocSite)) {
		info << "global:" << gv->getName().str();
	} else {
		info << "value:" << *mallocKey.allocSite;
	}

	info.flush();
}

void MemoryObject::print(std::ostream& out) const
{
	out	<< "MO. Name=" << name
		<< ". Addr=" << (void*)address << "-" << (void*)(address+size)
		<< ". Size=" << size
		<< ". ID=" << id
		<< ". Refs=" << refCount;
}



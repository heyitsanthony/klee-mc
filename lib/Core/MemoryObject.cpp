#include "Memory.h"
#include "MemoryManager.h"

#include "Context.h"
#include "klee/Common.h"
#include "klee/util/BitArray.h"

#include "ObjectHolder.h"

#include <llvm/Instruction.h>
#include <llvm/Value.h>
#include "llvm/BasicBlock.h"
#include "llvm/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <iostream>
#include <cassert>
#include <sstream>

using namespace llvm;
using namespace klee;

MemoryManager* MemoryObject::memoryManager = NULL;

int MemoryObject::counter = 0;

void MemoryObject::remove()
{
  assert(memoryManager);
  memoryManager->objects.erase(this);
}

void MemoryObject::getAllocInfo(std::string &result) const
{
  llvm::raw_string_ostream info(result);

  info << "MO" << id << "[" << size << "]";

  if (mallocKey.allocSite) {
    info << " allocated at ";
    if (const Instruction *i = dyn_cast<Instruction>(mallocKey.allocSite)) {
      info << i->getParent()->getParent()->getNameStr() << "():";
      info << *i;
    } else if (const GlobalValue *gv =
               dyn_cast<GlobalValue>(mallocKey.allocSite)) {
      info << "global:" << gv->getNameStr();
    } else {
      info << "value:" << *mallocKey.allocSite;
    }
  } else {
    info << " (no allocation info)";
  }
  
  info.flush();
}

void MemoryObject::print(std::ostream& out) const
{
  out << 
    "MO. Name=" << name << 
    ". Addr=" << (void*)address <<
    ". ID=" << id;
}

MemoryObject::MemoryObject(uint64_t _address) 
    : id(counter++),
      address(_address),
      size(0),
      mallocKey(0, 0, 0, false, false, true),
      refCount(0) 
{
}

MemoryObject::MemoryObject(
  uint64_t _address,
  unsigned _size, 
  const MallocKey &_mallocKey,
  HeapObject* _heapObj) 
    : id(counter++),
      address(_address),
      size(_size),
      name("unnamed"),
      mallocKey(_mallocKey),
      heapObj(ref<HeapObject>(_heapObj)),
      refCount(_mallocKey.isFixed ? 1 : 0),
      fake_object(false),
      isUserSpecified(false) 
{
}

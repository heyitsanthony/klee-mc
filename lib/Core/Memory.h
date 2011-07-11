//===-- Memory.h ------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_MEMORY_H
#define KLEE_MEMORY_H

#include "Context.h"

#include "klee/Expr.h"

#include "llvm/ADT/StringExtras.h"

#include <vector>
#include <string>
#include <map>
#include <list>

namespace llvm {
  class Value;
}

namespace klee {

class BitArray;
class MemoryManager;
class Solver;

class HeapObject {
  friend class ref<HeapObject>;

public:
  static MemoryManager* memoryManager;

private:
  friend class MemoryManager;
  friend class UpdateList;

  unsigned size;
  uint64_t address;
  unsigned align;
  mutable unsigned refCount;

public:
  HeapObject(unsigned _size, unsigned _align=0);
  ~HeapObject();
};

#include "MemoryObject.h"
#include "ObjectState.h"
} // End klee namespace

#endif

//===-- Memory.cpp --------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Memory.h"
#include "MemoryManager.h"

#include "Context.h"
#include "klee/Common.h"
#include "klee/Expr.h"
#include "klee/Solver.h"
#include "klee/util/BitArray.h"

#include "ObjectHolder.h"

#include <llvm/Function.h>
#include <llvm/Instruction.h>
#include <llvm/Value.h>
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "static/Sugar.h"

#include <sys/mman.h>
#include <iostream>
#include <cassert>
#include <sstream>

using namespace llvm;
using namespace klee;

/***/

MemoryManager* HeapObject::memoryManager = NULL;

HeapObject::HeapObject(unsigned _size, unsigned _align)
: size(_size)
, align(_align)
, refCount(0)
{
	if (!align) {
		address = (uint64_t) (unsigned long) malloc((unsigned) size);
	} else {
		assert (align == 12 && "Only handle page-level alignment");
		address = (uint64_t)mmap(
			NULL,
			(size + 4095) & ~0xfff,
			PROT_READ|PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS,
			-1,
			0);
		assert ((void*)address != MAP_FAILED);
	}

	memset((void*)address, 0, size); // so valgrind doesn't complain
}

HeapObject::~HeapObject()
{
	if (memoryManager) memoryManager->dropHeapObj(this);

	// free heap storage
	if (!align)
		free((void*)(uintptr_t)address);
	else
		munmap((void*)address, size);
}

/***/

ObjectHolder::ObjectHolder(const ObjectHolder &b) : os(b.os) {
  if (os) ++os->refCount;
}

ObjectHolder::ObjectHolder(ObjectState *_os) : os(_os) {
  if (os) ++os->refCount;
}

ObjectHolder::~ObjectHolder() {
  if (!os) return;
  if ( --os->refCount != 0) return;
  delete os;
}
 
ObjectHolder &ObjectHolder::operator=(const ObjectHolder &b) {
  if (b.os) ++b.os->refCount;
  if (os && --os->refCount==0) delete os;
  os = b.os;
  return *this;
}

/***/



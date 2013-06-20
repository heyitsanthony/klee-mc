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
#include "HeapMM.h"

#include "Context.h"
#include "klee/Common.h"
#include "klee/Expr.h"
#include "klee/Solver.h"
#include "klee/util/BitArray.h"

#include "ObjectHolder.h"

#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Value.h>
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "static/Sugar.h"

#include <sys/mman.h>
#include <iostream>
#include <cassert>
#include <sstream>

using namespace llvm;
using namespace klee;

#define CHK_32BIT_ADDR(x)	\
if (MemoryManager::is32Bit()) {	\
	assert ((x & ~((uint64_t)0xffffffff)) == 0);	\
}

unsigned long HeapObject::count = 0;

HeapMM* HeapObject::memoryManager = NULL;

static int getFlags(void)
{
	int	flags;

	flags = MAP_PRIVATE | MAP_ANONYMOUS;
	if (MemoryManager::is32Bit())
		flags |= MAP_32BIT;
	return flags;
}

HeapObject::HeapObject(unsigned _size, unsigned _align)
: size(_size)
, align(_align)
, refCount(0)
{
	count++;
	if (!align) {
		address = (uint64_t) (unsigned long) malloc((unsigned) size);
		CHK_32BIT_ADDR(address);
	} else {
		assert (align == 12 && "Only handle page-level alignment");

		address = (uint64_t)mmap(
			NULL,
			(size + 4095) & ~0xfff,
			PROT_READ|PROT_WRITE,
			getFlags(),
			-1,
			0);
		assert ((void*)address != MAP_FAILED);
	}

	memset((void*)address, 0, size); // so valgrind doesn't complain
}

HeapObject::~HeapObject()
{
	count--;

	if (memoryManager != NULL) {
		memoryManager->dropHeapObj(this);
	}

	// free heap storage
	if (!align)
		free((void*)(uintptr_t)address);
	else
		munmap((void*)address, size);
}

std::vector<HeapObject*> HeapObject::contiguousPages(unsigned int bytes)
{
	std::vector<HeapObject*>	ret;
	void				*addr;

	addr = mmap(
		NULL,
		(bytes + 4095) & ~0xfffULL,
		PROT_READ|PROT_WRITE,
		getFlags(),
		-1,
		0);
	assert (addr != MAP_FAILED);

	for (unsigned int i = 0; i < (bytes + 4095)/4096; i++) {
		HeapObject	*cur_page;
		cur_page = new HeapObject((void*)((intptr_t)addr + i*4096));
		ret.push_back(cur_page);
	}


	return ret;
}

HeapObject::HeapObject(void* page_addr)
: size(4096)
, address((uint64_t)page_addr)
, align(12)
, refCount(0)
{
	count++;
	CHK_32BIT_ADDR(address);
	memset(page_addr, 0, 4096);
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

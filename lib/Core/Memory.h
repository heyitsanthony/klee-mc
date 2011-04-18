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
class StateRecord;
class ConOffObjectWrite;
class SymOffObjectWrite;

class HeapObject {
  friend class ref<HeapObject>;

public:
  static MemoryManager* memoryManager;

private:
  friend class MemoryManager;
  friend class UpdateList;

  unsigned size;
  uint64_t address;
  mutable unsigned refCount;

public:
  HeapObject(unsigned _size);
  ~HeapObject();
};

class MemoryObject {
  friend class STPBuilder;
  friend class MemoryManager;

private:
  static int counter;
  static MemoryManager* memoryManager;

public:
  unsigned id;
  uint64_t address;

  /// size in bytes
  unsigned size;
  std::string name;

  MallocKey mallocKey;
  ref<HeapObject> heapObj;
  unsigned refCount;

  /// true if created by us.
  bool fake_object;
  bool isUserSpecified;

  /// A list of boolean expressions the user has requested be true of
  /// a counterexample. Mutable since we play a little fast and loose
  /// with allowing it to be added to during execution (although
  /// should sensibly be only at creation time).
  mutable std::vector< ref<Expr> > cexPreferences;

  // DO NOT IMPLEMENT
  MemoryObject(const MemoryObject &b);
  MemoryObject &operator=(const MemoryObject &b);

public:    
  // XXX this is just a temp hack, should be removed
  explicit
  MemoryObject(uint64_t _address) 
    : id(counter++),
      address(_address),
      size(0),
      mallocKey(0, 0, 0, false, false, true),
      refCount(0) {
  }

  MemoryObject(uint64_t _address, unsigned _size, const MallocKey &_mallocKey,
               HeapObject* _heapObj = NULL) 
    : id(counter++),
      address(_address),
      size(_size),
      name("unnamed"),
      mallocKey(_mallocKey),
      heapObj(ref<HeapObject>(_heapObj)),
      refCount(_mallocKey.isFixed ? 1 : 0),
      fake_object(false),
      isUserSpecified(false) {
  }

  // Ugh. But we can't include this in the inlined dtor or we'll have a circular
  // dependency between Memory.h and MemoryManager.h
  void remove();

  inline ~MemoryObject() {
    if(size && memoryManager)
      remove();
  }

  /// Get an identifying string for this allocation.
  void getAllocInfo(std::string &result) const;

  void setName(std::string name) {
    this->name = name;
  }

  ref<ConstantExpr> getBaseExpr() const { 
    return ConstantExpr::create(address, Context::get().getPointerWidth());
  }
  ref<ConstantExpr> getSizeExpr() const { 
    return ConstantExpr::create(size, Context::get().getPointerWidth());
  }
  ref<Expr> getOffsetExpr(ref<Expr> pointer) const {
    return SubExpr::create(pointer, getBaseExpr());
  }
  ref<Expr> getBoundsCheckPointer(ref<Expr> pointer) const {
    return getBoundsCheckOffset(getOffsetExpr(pointer));
  }
  ref<Expr> getBoundsCheckPointer(ref<Expr> pointer, unsigned bytes) const {
    return getBoundsCheckOffset(getOffsetExpr(pointer), bytes);
  }

  ref<Expr> getBoundsCheckOffset(ref<Expr> offset) const {
    if (size==0) {
      return EqExpr::create(offset, 
                            ConstantExpr::alloc(0, Context::get().getPointerWidth()));
    } else {
      return UltExpr::create(offset, getSizeExpr());
    }
  }
  ref<Expr> getBoundsCheckOffset(ref<Expr> offset, unsigned bytes) const {
    if (bytes<=size) {
      return UltExpr::create(offset, 
                             ConstantExpr::alloc(size - bytes + 1, 
                                                 Context::get().getPointerWidth()));
    } else {
      return ConstantExpr::alloc(0, Expr::Bool);
    }
  }
  
  inline bool isLocal() const { return mallocKey.isLocal; }
  inline bool isGlobal() const { return mallocKey.isGlobal; }
  inline void setGlobal(bool _isGlobal) { mallocKey.isGlobal = _isGlobal; }
  inline const llvm::Value* getAllocSite() const { return mallocKey.allocSite; }
};

class ObjectState {
    friend class AddressSpace;
    friend class ExecutionState;
    friend class StateRecord;
    friend class LiveSetCache;
private:
    
  unsigned copyOnWriteOwner; // exclusively for AddressSpace

  friend class ObjectHolder;
  unsigned refCount;

  const MemoryObject *object;

  uint8_t *concreteStore;
  // XXX cleanup name of flushMask (its backwards or something)
  BitArray *concreteMask;

  // mutable because may need flushed during read of const
  mutable BitArray *flushMask;

  ref<Expr> *knownSymbolics;

  // mutable because we may need flush during read of const
  mutable UpdateList updates;

public:
  unsigned size;

  bool readOnly;

  StateRecord* allocRec;
  std::map<unsigned,ConOffObjectWrite*> conOffObjectWrites;
  std::list<SymOffObjectWrite*> symOffObjectWrites;
  unsigned wrseqno;
  bool wasSymOffObjectWrite;

public:
  /// Create a new object state for the given memory object with concrete
  /// contents. The initial contents are undefined, it is the callers
  /// responsibility to initialize the object contents appropriately.
  ObjectState(const MemoryObject *mo, StateRecord* _allocRec);

  /// Create a new object state for the given memory object with symbolic
  /// contents.
  ObjectState(const MemoryObject *mo, const Array *array);

  ObjectState(const ObjectState &os);
  ~ObjectState();

  bool equals(const ObjectState* o1) const;
  const MemoryObject *getObject() const { return object; }

  void setReadOnly(bool ro) { readOnly = ro; }

  // make contents all concrete and zero
  void initializeToZero();
  // make contents all concrete and random
  void initializeToRandom();  

private:
  ref<Expr> read(ref<Expr> offset, Expr::Width width, StateRecord* rec) const;
  ref<Expr> read(unsigned offset, Expr::Width width, StateRecord* rec) const;
  ref<Expr> read8(unsigned offset, StateRecord* rec) const;

  // return bytes written.
  void write(unsigned offset, ref<Expr> value, StateRecord* rec);
  void write(ref<Expr> offset, ref<Expr> value, StateRecord* rec);

  void write8(unsigned offset, uint8_t value, StateRecord* rec);
  void write16(unsigned offset, uint16_t value, StateRecord* rec);
  void write32(unsigned offset, uint32_t value, StateRecord* rec);
  void write64(unsigned offset, uint64_t value, StateRecord* rec);

private:
  const UpdateList &getUpdates() const;

  void makeConcrete();

  void makeSymbolic();

  ref<Expr> read8(ref<Expr> offset, StateRecord* rec) const;
  void write8(unsigned offset, ref<Expr> value, StateRecord* rec);
  void write8(ref<Expr> offset, ref<Expr> value, StateRecord* rec);

  void fastRangeCheckOffset(ref<Expr> offset, unsigned *base_r, 
                            unsigned *size_r) const;
  void flushRangeForRead(unsigned rangeBase, unsigned rangeSize) const;
  void flushRangeForWrite(unsigned rangeBase, unsigned rangeSize);

  bool isByteConcrete(unsigned offset) const;
  bool isByteFlushed(unsigned offset) const;
  bool isByteKnownSymbolic(unsigned offset) const;

  void markByteConcrete(unsigned offset);
  void markByteSymbolic(unsigned offset);
  void markByteFlushed(unsigned offset);
  void markByteUnflushed(unsigned offset);
  void setKnownSymbolic(unsigned offset, Expr *value);

  void print();
};
  
} // End klee namespace

#endif

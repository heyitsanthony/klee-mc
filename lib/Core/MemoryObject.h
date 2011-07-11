#ifndef MEMORYOBJECT_H
#define MEMORYOBJECT_H

class MemoryObject
{
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

private:
  friend class ref<MemoryObject>;
  unsigned refCount;

public:
  // XXX this is just a temp hack, should be removed
  explicit MemoryObject(uint64_t _address);

  MemoryObject(
    uint64_t _address,
    unsigned _size,
    const MallocKey &_mallocKey,
    HeapObject* _heapObj = NULL);

  // Ugh. But we can't include this in the inlined dtor or we'll have a circular
  // dependency between Memory.h and MemoryManager.h
  void remove();

  inline ~MemoryObject() {
    if(size && memoryManager)
      remove();
  }

  void print(std::ostream& out) const;

  /// Get an identifying string for this allocation.
  void getAllocInfo(std::string &result) const;

  void setName(std::string name) {
    this->name = name;
  }

  unsigned int getRefCount(void) const { return refCount; }

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

#endif

#ifndef MEMORYOBJECT_H
#define MEMORYOBJECT_H

class MemoryObject
{
  friend class STPBuilder;
  friend class MemoryManager;

private:
  static unsigned		counter;
  static unsigned		numMemObjs;
  static MemoryManager		*memoryManager;

public:
  unsigned	id;
  unsigned	size;	// in bytes
  uint64_t	address;
  std::string	name;
  MallocKey	mallocKey;

  /// true if created by us.
  bool		fake_object;
  bool		isUserSpecified;

  /// A list of boolean expressions the user has requested be true of
  /// a counterexample. Mutable since we play a little fast and loose
  /// with allowing it to be added to during execution (although
  /// should sensibly be only at creation time).
  mutable std::vector< ref<Expr> > cexPreferences;

private:
  friend class ref<MemoryObject>;
   unsigned refCount;

   mutable ref<ConstantExpr>	base_ptr;

  // DO NOT IMPLEMENT
  MemoryObject(const MemoryObject &b);
  MemoryObject &operator=(const MemoryObject &b);

public:
  // XXX this is just a temp hack, should be removed
  explicit MemoryObject(uint64_t _address);

  MemoryObject(
    uint64_t _address,
    unsigned _size,
    const MallocKey &_mallocKey);

  // Ugh. But we can't include this in the inlined dtor or we'll have a circular
  // dependency between Memory.h and MemoryManager.h
  void remove();

  ~MemoryObject();

  void print(std::ostream& out) const;

  /// Get an identifying string for this allocation.
  void getAllocInfo(std::string &result) const;

  void setName(std::string name) { this->name = name; }

  unsigned int getRefCount(void) const { return refCount; }

  ref<ConstantExpr> getBaseExpr(void) const
  {	if (base_ptr.isNull())
		base_ptr = MK_CONST(address, Context::get().getPointerWidth());
	return base_ptr; }

  ref<ConstantExpr> getSizeExpr() const
  { return MK_CONST(size, Context::get().getPointerWidth()); }

  ref<Expr> getOffsetExpr(ref<Expr> pointer) const
  { return SubExpr::create(pointer, getBaseExpr()); }

  int64_t getOffset(uint64_t p) const { return (int64_t)(p - address); }
  bool isInBounds(uint64_t p, unsigned bytes) const
  {
  	int64_t	off;
  	if (bytes > size) return false;
	off = getOffset(p);
	if (off < 0) return false;
	return (off < (size - bytes + 1));
  }
  
  ref<Expr> getBoundsCheckPointer(ref<Expr> pointer, unsigned bytes) const
  { return getBoundsCheckOffset(getOffsetExpr(pointer), bytes); }

  ref<Expr> getBoundsCheckOffset(ref<Expr> offset, unsigned bytes) const {
	if (bytes > size)
		return ConstantExpr::alloc(0, Expr::Bool);

	return UltExpr::create(
		offset,
		ConstantExpr::alloc(
			size - bytes + 1,
			Context::get().getPointerWidth()));
  }

  inline bool isLocal() const { return mallocKey.isLocal; }
  inline bool isGlobal() const { return mallocKey.isGlobal; }
  inline void setGlobal(bool _isGlobal) { mallocKey.isGlobal = _isGlobal; }
  inline const llvm::Value* getAllocSite() const { return mallocKey.allocSite; }

  int compare(const MemoryObject& mo) const
  {
	if (address < mo.address) return -1;
	else if (address == mo.address) return 0;
	return 1;
  }
};

#endif

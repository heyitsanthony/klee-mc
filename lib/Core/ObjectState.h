#ifndef OBJSTATE_H
#define OBJSTATE_H

#ifndef KLEE_MEMORY_H
#error Never include objstate.h; use memory.h
#endif

#define COW_ZERO	~((unsigned)0)

/* track list of all object states */
// #define KEEP_OBJLIST

class ObjectStateAlloc
{
public:
	ObjectStateAlloc() {}
	virtual ~ObjectStateAlloc() {}
	virtual ObjectState* create(unsigned size) = 0;
	virtual ObjectState* create(unsigned size, const ref<Array>& arr) = 0;
	virtual ObjectState* create(const ObjectState& os) = 0;
};

template <class T>
class ObjectStateFactory : public ObjectStateAlloc
{
public:
	ObjectStateFactory() {}
	virtual ~ObjectStateFactory() {}
	virtual T* create(unsigned size) { return new T(size); }
	virtual T* create(unsigned size, const ref<Array>& arr)
	{ return new T(size, arr); }
	virtual T* create(const ObjectState& os) { return new T(os); }
};

class ObjectState
{
private:
	friend class ObjectHolder;
	friend class ObjectStateFactory<ObjectState>;

	typedef std::list<const ObjectState*> objlist_ty;
	static unsigned		numObjStates;
	static ObjectStateAlloc	*os_alloc;
	static std::unique_ptr<ObjectState>	zero_page;

	const ref<Array>	src_array;
	unsigned		copyOnWriteOwner;
	unsigned		refCount;
	unsigned		copyDepth;

	std::unique_ptr<uint8_t[]> 	concreteStore;
	std::unique_ptr<BitArray>	concreteMask;

	// mutable because may need flushed during read of const
	// XXX cleanup name of flushMask (its backwards or something) ???
	mutable std::unique_ptr<BitArray> flushMask;

	std::unique_ptr<ref<Expr>[]>	knownSymbolics;

	// mutable because we may need flush during read of const
	mutable UpdateList	updates;

#ifdef KEEP_OBJLIST
	static objlist_ty	objs;		/* all active objects */
	objlist_ty::iterator	objs_it;	/* obj's position in list */
#endif
public:
	bool		readOnly;

public:
	static ObjectState* create(unsigned size);
	static ObjectState* create(unsigned size, const ref<Array>& arr);
	static ObjectState* create(const ObjectState& os);

protected:
	/// Create a new object state for the given memory object with concrete
	/// contents. The initial contents are undefined;
	//  the caller initializes the object contents appropriately.
	ObjectState(unsigned size);

	/// Create object state for memory object with symbolic contents.
	ObjectState(unsigned size, const ref<Array> &array);

	ObjectState(const ObjectState &os);

	const unsigned	size;
public:
	virtual ~ObjectState();

	static unsigned getNumObjStates(void) { return numObjStates; }

	unsigned getSize(void) const { return size; }
	void setReadOnly(bool ro) { readOnly = ro; }
	bool isReadOnly(void) const { return readOnly; }

	// make contents all concrete and zero
	void initializeToZero();
	// make contents all concrete and random
	void initializeToRandom();

	unsigned int getNumConcrete(void) const;
	bool isByteConcrete(unsigned offset) const;
	bool isByteFlushed(unsigned offset) const;
	bool isByteKnownSymbolic(unsigned offset) const;
	bool isConcrete(void) const { return concreteMask == NULL; }

	void markRangeSymbolic(unsigned offset, unsigned len);

	bool writeIVC(unsigned offset, const ref<ConstantExpr>& ce);

	ref<Expr> read(ref<Expr> offset, Expr::Width width) const;
	ref<Expr> read(unsigned offset, Expr::Width width) const;

	virtual ref<Expr> read8(unsigned offset) const;

	uint8_t	read8c(unsigned off) const;
	void write8(unsigned offset, uint8_t value);

	const ref<Array> getArrayRef(void) const { return src_array; }
	const Array* getArray(void) const { return src_array.get(); }
	unsigned getRefCount(void) const { return refCount; }

	void print(unsigned int begin = 0, int end = -1) const;
	void printDiff(const ObjectState& os) const;
	unsigned hash(void) const;

	static void setupZeroObjs(void);
	static void setAlloc(ObjectStateAlloc* alloc) { os_alloc = alloc; }
	static ObjectStateAlloc* getAlloc(void) { return os_alloc; }

	static ObjectState* createDemandObj(unsigned sz);
	static void garbageCollect(void);

	bool isZeroPage(void) const { return copyOnWriteOwner == COW_ZERO; }

	virtual void write(unsigned offset, const ref<Expr>& value);
	void write(ref<Expr> offset, const ref<Expr>& value);
	void writeConcrete(const uint8_t* addr, unsigned wr_sz);
	void readConcrete(uint8_t* addr, unsigned rd_sz, unsigned off=0) const;
	int readConcreteSafe(uint8_t* addr, unsigned rd_sz, unsigned off=0) const;
	int cmpConcrete(const uint8_t* addr, unsigned sz, unsigned off=0) const;

	const uint8_t* getConcreteBuf(void) const { return concreteStore.get(); }

	int cmpConcrete(const ObjectState& os) const;

	void setOwner(unsigned _new_cow) { copyOnWriteOwner = _new_cow; }
	bool hasOwner(void) const { return copyOnWriteOwner != 0; }

	bool isOwner(unsigned cowkey) const { return cowkey==copyOnWriteOwner; }
	unsigned getOwner(void) const { return copyOnWriteOwner; }
	bool revertToConcrete(void);
	bool isRevertible(void) const;

	static bool revertToConcrete(const ObjectState* os);

	unsigned getCopyDepth(void) const { return copyDepth; }
	void incCopyDepth(void) { copyDepth++; }
	void resetCopyDepth(void) { copyDepth = 0; }
protected:
	void buildUpdates(void) const;


	void flushWriteByte(unsigned offset);
	const UpdateList &getUpdates() const;

	void makeConcrete();
	void makeSymbolic();

	virtual ref<Expr> read8(ref<Expr> offset) const;
	ref<Expr> readSlow(ref<Expr>& offset, Expr::Width width) const;
	virtual void write8(unsigned offset, ref<Expr>& value);
	void write8(ref<Expr> offset, ref<Expr>& value);
	ref<Expr> readConstantBytes(unsigned offset, unsigned NumBytes) const;


	void fastRangeCheckOffset(
		ref<Expr> offset, unsigned *base_r, unsigned *size_r) const;
	void flushRangeForRead(unsigned rangeBase, unsigned rangeSize) const;
	void flushRangeForWrite(unsigned rangeBase, unsigned rangeSize);
	void flushForRead(void) const { flushRangeForRead(0, size); }

	void markByteConcrete(unsigned offset);
	void markByteSymbolic(unsigned offset);
	void markByteFlushed(unsigned offset);
	void markByteUnflushed(unsigned offset);
	void setKnownSymbolic(unsigned offset, Expr *value);
};

#endif

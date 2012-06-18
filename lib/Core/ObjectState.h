#ifndef OBJSTATE_H
#define OBJSTATE_H

#ifndef KLEE_MEMORY_H
#error Never include objstate.h; use memory.h
#endif

#define COW_ZERO	~((unsigned)0)

class ObjectState
{
	friend class AddressSpace;
	friend class ExecutionState;
	friend class Executor;

private:
	friend class ObjectHolder;

	const ref<Array>	src_array;
	// exclusively for AddressSpace
	unsigned		copyOnWriteOwner;
	unsigned		refCount;

	uint8_t			*concreteStore;
	BitArray		*concreteMask;
	// mutable because may need flushed during read of const
	// XXX cleanup name of flushMask (its backwards or something)
	mutable BitArray	*flushMask;
	ref<Expr>		*knownSymbolics;

	// mutable because we may need flush during read of const
	mutable UpdateList	updates;

	static unsigned		numObjStates;
	static ObjectState	*zeroPage;

public:
	unsigned	size;
	bool		readOnly;

public:
	/// Create a new object state for the given memory object with concrete
	/// contents. The initial contents are undefined, it is the callers
	/// responsibility to initialize the object contents appropriately.
	ObjectState(unsigned size);

	/// Create a new object state for the given memory object with symbolic
	/// contents.
	ObjectState(unsigned size, const ref<Array> &array);

	ObjectState(const ObjectState &os);
	~ObjectState();

	static unsigned getNumObjStates(void) { return numObjStates; }

	void setReadOnly(bool ro) { readOnly = ro; }

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
	ref<Expr> read8(unsigned offset) const;
	uint8_t	read8c(unsigned off) const;
	void write8(unsigned offset, uint8_t value);

	const Array* getArray(void) const { return src_array.get(); }
	void print(unsigned int begin = 0, int end = -1) const;

	unsigned hash(void) const;

	static void setupZeroObjs(void);
	static ObjectState* createDemandObj(unsigned sz);

	bool isZeroPage(void) const { return copyOnWriteOwner == COW_ZERO; }
private:

	// return bytes written.
	void write(unsigned offset, ref<Expr> value);
	void write(ref<Expr> offset, ref<Expr> value);

	void write16(unsigned offset, uint16_t value);
	void write32(unsigned offset, uint32_t value);
	void write64(unsigned offset, uint64_t value);

	void flushWriteByte(unsigned offset);
	const UpdateList &getUpdates() const;

	void makeConcrete();
	void makeSymbolic();

	ref<Expr> read8(ref<Expr> offset) const;
	void write8(unsigned offset, ref<Expr> value);
	void write8(ref<Expr> offset, ref<Expr> value);

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

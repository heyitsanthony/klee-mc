//===-- AddressSpace.h ------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_ADDRESSSPACE_H
#define KLEE_ADDRESSSPACE_H

#include <stack>
#include "ObjectHolder.h"

#include "klee/Expr.h"
#include "klee/Internal/ADT/ImmutableMap.h"

namespace klee
{
class ExecutionState;
class MemoryObject;
class ObjectState;
class StateSolver;

template<class T> class ref;

#define op_mo(x)	x.first
#define op_os(x)	x.second
typedef std::pair<const MemoryObject*, const ObjectState*> ObjectPair;

typedef std::vector<ObjectPair> ResolutionList;

/// Function object ordering MemoryObject's by address.
struct MemoryObjectLT
{ bool operator()(const MemoryObject *a, const MemoryObject *b) const; };

typedef ImmutableMap<
	const MemoryObject*, ObjectHolder, MemoryObjectLT> MemoryMap;
typedef MemoryMap::iterator		MMIter;

class AddressSpace
{
	friend class ExecutionState;
	friend class ObjectState;
	friend class SymAddrSpace;
private:
	/// Epoch counter used to control ownership of objects.
	mutable unsigned cowKey;
	/**
	 * Counts the number of modifications made to the AS since fork.
	 * This is primarily used by the TLB to track changes. */
	unsigned	os_generation;
	unsigned	mo_generation;

	const MemoryObject* last_mo;

	/// Unsupported, use copy constructor
	AddressSpace &operator=(const AddressSpace&);

	/// The MemoryObject -> ObjectState map that constitutes the
	/// address space.
	///
	/// The set of objects where o->copyOnWriteOwner == cowKey are the
	/// objects that we own.
	///
	/// \invariant forall o in objects, o->copyOnWriteOwner <= cowKey
	MemoryMap objects;
public:
	AddressSpace()
	: cowKey(1)
	, os_generation(0)
	, mo_generation(0)
	, last_mo(NULL) {}

	AddressSpace(const AddressSpace &b)
	: cowKey(++b.cowKey)
	, os_generation(b.os_generation)
	, mo_generation(b.mo_generation)
	, last_mo(NULL)
	, objects(b.objects)
	{ }

	virtual ~AddressSpace() {}

	/// Resolve address to an ObjectPair in result.
	/// \return true iff an object was found.
	bool resolveOne(uint64_t address, ObjectPair& result) const;
	const MemoryObject* resolveOneMO(uint64_t address) const;
	const MemoryObject* getLastBoundHint(void) const { return last_mo; }

	void clear(void);

	Expr::Hash hash(void) const;
	unsigned getGeneration(void) const { return os_generation; }
	unsigned getGenerationMO(void) const { return mo_generation; }
private:
	/// Add a binding to the address space.
	void bindObject(const MemoryObject *mo, ObjectState *os);

	bool lookupGuess(uint64_t example, const MemoryObject* &mo);

	bool testInBoundPointer(
		ExecutionState &state,
		StateSolver *solver,
		ref<Expr> address,
		ref<ConstantExpr>& c_addr,
		const MemoryObject*	&mo);

	bool isFeasibleRange(
		ExecutionState &state,
		StateSolver *solver,
		ref<Expr> address,
		const MemoryObject* lo,
		const MemoryObject* hi,
		bool& ok);

	MMIter getMidPoint(MMIter& begin, MMIter& end);

public:
	/// Remove a binding from the address space.
	void unbindObject(const MemoryObject *mo);

	/* an integrity check to make sure that object states have
	 * sizes >= memory objects */
	void checkObjects(void) const;

	MMIter lower_bound(uint64_t addr) const;
	MMIter upper_bound(uint64_t addr) const;
	MMIter end(void) const { return objects.end(); }
	MMIter begin(void) const { return objects.begin(); }

	void printAddressInfo(std::ostream& os, uint64_t addr) const;
	void printObjects(std::ostream& os) const;

	/// Lookup a binding from a MemoryObject.
	const ObjectState *findObject(const MemoryObject *mo) const;
	ObjectState* findWriteableObject(const MemoryObject* mo);

	/// \brief Obtain an ObjectState suitable for writing.
	///
	/// This returns a writeable object state, creating a new copy of
	/// the given ObjectState if necessary. If the address space owns
	/// the ObjectState then this routine effectively just strips the
	/// const qualifier it.
	///
	/// \param mo The MemoryObject to get a writeable ObjectState for.
	/// \param os The current binding of the MemoryObject.
	/// \return A writeable ObjectState (\a os or a copy).
	ObjectState *getWriteable(const MemoryObject *mo, const ObjectState *os);
	ObjectState *getWriteable(const ObjectPair& op)
	{ return getWriteable(op.first, op.second); }

	bool copyToBuf(const MemoryObject* mo, void* buf) const;
	bool copyToBuf(
	const MemoryObject* mo, void* buf,
	unsigned off, unsigned len) const;

	void copyToExprBuf(
		const MemoryObject* mo, ref<Expr>* buf,
		unsigned off, unsigned len) const;


	/* returns number of bytes copied into address space */
	unsigned int copyOutBuf(
		uint64_t	addr,
		const char	*bytes,
		unsigned int	len);


	/// Copy the concrete values of all managed ObjectStates into the
	/// actual system memory location they were allocated at.
	void copyOutConcretes();

	/// Copy the concrete values of all managed ObjectStates back from
	/// the actual system memory location they were allocated
	/// at. ObjectStates will only be written to (and thus,
	/// potentially copied) if the memory values are different from
	/// the current concrete values.
	///
	/// \retval true The copy succeeded.
	/// \retval false The copy failed because a read-only object was modified.
	bool copyInConcretes(void);
	void print(std::ostream& os) const;

	/* scan for 16*n byte extents of 'magic' byte 0xa3 */
	std::vector<std::pair<void*, unsigned> > getMagicExtents(void);

	bool readConcrete(
		std::vector<uint8_t>& v,
		std::vector<bool>& is_conc,
		uint64_t addr,
		unsigned len) const;

	int readConcreteSafe(
		uint8_t* buf,
		uint64_t addr,
		unsigned len) const;


};
} // End klee namespace

#endif

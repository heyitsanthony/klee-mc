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
class TimingSolver;

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
private:
	/// Epoch counter used to control ownership of objects.
	mutable unsigned cowKey;
	/**
	 * Counts the number of modifications made to the AS since fork.
	 * This is primarily used by the TLB to track changes. */
	unsigned	generation;

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
	AddressSpace() : cowKey(1), generation(0), last_mo(NULL) {}
	AddressSpace(const AddressSpace &b)
	: cowKey(++b.cowKey)
	, generation(0)
	, last_mo(NULL)
	, objects(b.objects)
	{ }
	~AddressSpace() {}

	/// Resolve address to an ObjectPair in result.
	/// \return true iff an object was found.
	bool resolveOne(const ref<ConstantExpr> &address, ObjectPair &result);
	bool resolveOne(uint64_t address, ObjectPair& result);
	const MemoryObject* resolveOneMO(uint64_t address);
	const MemoryObject* getLastBoundHint(void) const { return last_mo; }

	// Find a feasible object for 'address'.
	// Returns 'true' if feasible object is found
	bool getFeasibleObject(
		ExecutionState &state,
		TimingSolver *solver,
		ref<Expr> address,
		ObjectPair &result);

	/// Resolve address to a list of ObjectPairs it can point to. If
	/// maxResolutions is non-zero then no more than that many pairs
	/// will be returned.
	///
	/// \return true iff the resolution is incomplete (maxResolutions
	/// is non-zero and the search terminated early, or a query timed out).
	bool resolve(
		ExecutionState &state,
		TimingSolver *solver,
		ref<Expr> address,
		ResolutionList &rl,
		unsigned maxResolutions=0);

	ref<Expr> getOOBCond(ref<Expr>& symptr) const;
	unsigned hash(void) const;
	unsigned getGeneration(void) const { return generation; }
private:
	/// Add a binding to the address space.
	void bindObject(const MemoryObject *mo, ObjectState *os);

	bool lookupGuess(uint64_t example, const MemoryObject* &mo);

	bool testInBoundPointer(
		ExecutionState &state,
		TimingSolver *solver,
		ref<Expr> address,
		ref<ConstantExpr>& c_addr,
		const MemoryObject*	&mo);

	bool isFeasibleRange(
		ExecutionState &state,
		TimingSolver *solver,
		ref<Expr> address,
		const MemoryObject* lo,
		const MemoryObject* hi,
		bool& ok);

	bool mustContain(
		ExecutionState &state,
		TimingSolver* solver,
		ref<Expr> address,
		const MemoryObject* mo,
		bool& ok)
	{ return mustContain(state, solver, address, mo, mo, ok); }

	bool mustContain(
		ExecutionState &state,
		TimingSolver* solver,
		ref<Expr> address,
		const MemoryObject* lo,
		const MemoryObject* hi,
		bool& ok);


	bool isFeasible(
		ExecutionState &state,
		TimingSolver* solver,
		ref<Expr> address,
		const MemoryObject* mo,
		bool& ok)
	{ return isFeasibleRange(state, solver, address, mo, mo, ok); }

	MMIter getMidPoint(MMIter& begin, MMIter& end);

	ref<Expr> getFeasibilityExpr(
		ref<Expr> address,
		const MemoryObject* lo,
		const MemoryObject* hi) const;

	bool binsearchRange(
		ExecutionState& state,
		ref<Expr>	p,
		TimingSolver *solver,
		std::stack<std::pair<MMIter, MMIter> >& tryRanges,
		unsigned int maxResolutions,
		ResolutionList& rl);

	bool binsearchFeasible(
		ExecutionState& state,
		TimingSolver* solver,
		ref<Expr>& addr,
		uint64_t upper_addr, ObjectPair& res);


	bool contigOffsetSearchRange(
		ExecutionState& state,
		ref<Expr>	p,
		TimingSolver *solver,
		ResolutionList& rl,
		bool& bad_addr);

public:
	/// Remove a binding from the address space.
	void unbindObject(const MemoryObject *mo);

	MMIter lower_bound(uint64_t addr) const;
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


};
} // End klee namespace

#endif

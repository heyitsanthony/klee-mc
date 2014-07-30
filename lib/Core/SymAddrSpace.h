#ifndef KLEE_SADDRESSSPACE_H
#define KLEE_SADDRESSSPACE_H

#include <stack>
#include "ObjectHolder.h"

#include "klee/Expr.h"
#include "klee/Internal/ADT/ImmutableMap.h"
#include "AddressSpace.h"

namespace klee
{
class ExecutionState;
class MemoryObject;
class ObjectState;
class StateSolver;

class SymAddrSpace
{
private:
	/// Unsupported, use copy constructor
	SymAddrSpace(StateSolver* _s, ExecutionState& _es)
	: solver(_s), es(_es)  {}
public:
	// Find a feasible object for 'address'.
	// Returns 'true' if feasible object is found
	static bool getFeasibleObject(
		ExecutionState &state,
		StateSolver *solver,
		ref<Expr> address,
		ObjectPair &result);

	/// Resolve address to a list of ObjectPairs it can point to. If
	/// maxResolutions is non-zero then no more than that many pairs
	/// will be returned.
	///
	/// \return true iff the resolution is incomplete (maxResolutions
	/// is non-zero and the search terminated early, or a query timed out).
	static bool resolve(
		ExecutionState &state,
		StateSolver *solver,
		ref<Expr> address,
		ResolutionList &rl,
		unsigned maxResolutions=0);

	static ref<Expr> getOOBCond(const AddressSpace& as, ref<Expr>& symptr);
private:
	bool getFeasibleObject(ref<Expr>& address, ObjectPair &result);

	bool resolve(
		ref<Expr>& address,
		ResolutionList &rl,
		unsigned maxResolutions);

	bool testInBoundPointer(
		ref<Expr> address,
		ref<ConstantExpr>& c_addr,
		const MemoryObject*	&mo);

	bool isFeasibleRange(
		ref<Expr> address,
		const MemoryObject* lo,
		const MemoryObject* hi,
		bool& ok);

	bool mustContain(
		ref<Expr> address,
		const MemoryObject* mo,
		bool& ok)
	{ return mustContain(address, mo, mo, ok); }

	bool mustContain(
		ref<Expr> address,
		const MemoryObject* lo,
		const MemoryObject* hi,
		bool& ok);


	bool isFeasible(
		ref<Expr> address,
		const MemoryObject* mo,
		bool& ok)
	{ return isFeasibleRange(address, mo, mo, ok); }

	ref<Expr> getFeasibilityExpr(
		ref<Expr> address,
		const MemoryObject* lo,
		const MemoryObject* hi) const;

	bool binsearchRange(
		ref<Expr>	p,
		std::stack<std::pair<MMIter, MMIter> >& tryRanges,
		unsigned int maxResolutions,
		ResolutionList& rl);

	bool binsearchFeasible(
		ref<Expr>& addr,
		uint64_t upper_addr, ObjectPair& res);


	bool contigOffsetSearchRange(
		ref<Expr> p, ResolutionList& rl, bool& bad_addr);

	StateSolver	*solver;
	ExecutionState	&es;
};
} // End klee namespace

#endif

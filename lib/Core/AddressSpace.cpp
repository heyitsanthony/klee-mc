//===-- AddressSpace.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/ExecutionState.h"
#include "AddressSpace.h"
#include "CoreStats.h"
#include "Memory.h"
#include "TimingSolver.h"
#include "klee/Expr.h"
#include "klee/TimerStatIncrementer.h"
#include <stdint.h>
#include <stack>
#include "static/Sugar.h"

using namespace klee;

///

void AddressSpace::bindObject(const MemoryObject *mo, ObjectState *os) {
  assert(os->copyOnWriteOwner == 0 && "object already has owner");
  os->copyOnWriteOwner = cowKey;
  objects = objects.replace(std::make_pair(mo, os));
}

void AddressSpace::unbindObject(const MemoryObject *mo) {
  objects = objects.remove(mo);
}

const ObjectState *AddressSpace::findObject(const MemoryObject *mo) const {
  const MemoryMap::value_type *res = objects.lookup(mo);

  return res ? res->second : 0;
}

ObjectState *AddressSpace::findObject(const MemoryObject *mo) {
  const MemoryMap::value_type *res = objects.lookup(mo);
  return res ? res->second : 0;
}

ObjectState *AddressSpace::getWriteable(
	const MemoryObject *mo,
	const ObjectState *os)
{
	ObjectState	*n;

	assert(!os->readOnly);

	if (cowKey == os->copyOnWriteOwner)
		return const_cast<ObjectState*> (os);

	n = new ObjectState(*os);
	n->copyOnWriteOwner = cowKey;
	objects = objects.replace(std::make_pair(mo, n));
	return n;
}

bool AddressSpace::resolveOne(uint64_t address, ObjectPair &result)
{
	MemoryObject			toFind(address);
	const MemoryMap::value_type 	*res;
	const MemoryObject		*mo;

	res = objects.lookup_previous(&toFind);
	if (!res) return false;

	mo = res->first;
	if (	(mo->size == 0 && address == mo->address) ||
		(address - mo->address < mo->size))
	{
		result = *res;
		return true;
	}

	return false;
}

const MemoryObject* AddressSpace::resolveOneMO(uint64_t address)
{
	ObjectPair	result;
	if (!resolveOne(address, result))
		return NULL;
	return result.first;
}

/// fucking hell fix this
bool AddressSpace::resolveOne(const ref<ConstantExpr> &addr, ObjectPair &result)
{
	return resolveOne(addr->getZExtValue(), result);
}

MMIter AddressSpace::getMidPoint(
	MMIter& range_begin,
	MMIter& range_end)
{
	// find the midpoint between ei and bi
	MMIter mid = range_begin;
	bool even = true;
	foreach (it, range_begin, range_end) {
		even = !even;
		if (even) ++mid;
	}

	return mid;
}

ref<Expr> AddressSpace::getFeasibilityExpr(
	ref<Expr> address,
	const MemoryObject* lo,
	const MemoryObject* hi) const
{
	/* address >= low->base &&
	 * address < high->base+high->size) */
	ref<Expr> inRange =
	AndExpr::create(
		UgeExpr::create(
			address,
			lo->getBaseExpr()),
		UltExpr::create(
			address,
			AddExpr::create(
				hi->getBaseExpr(),
				hi->getSizeExpr())));
	return inRange;
}

bool AddressSpace::isFeasibleRange(
	ExecutionState &state,
	TimingSolver *solver,
	ref<Expr> address,
	const MemoryObject* lo, const MemoryObject* hi)
{
	bool mayBeTrue, ok;

	ref<Expr> inRange = getFeasibilityExpr(address, lo, hi);

	ok = solver->mayBeTrue(state, inRange, mayBeTrue);
	if (!ok) {
		assert (0 == 1 && "Solver broke down");
		return false; // query error
	}

	return mayBeTrue;
}

// try cheap search, will succeed for *any* inbounds pointer
// success => mo != NULL
bool AddressSpace::testInBoundPointer(
	ExecutionState		&state,
	TimingSolver		*solver,
	ref<Expr>		address,
	ref<ConstantExpr>	&c_addr,
	const MemoryObject*	&mo)
{
	const MemoryMap::value_type	*res;
	uint64_t			example;

	mo = NULL;
	if (!solver->getValue(state, address, c_addr)) {
		c_addr = ConstantExpr::create(~0ULL, address->getWidth());
		return false;
	}

	example = c_addr->getZExtValue();
	MemoryObject toFind(example);
	res = objects.lookup_previous(&toFind);
	if (!res) return true;

	mo = res->first;
	if (example < mo->address + mo->size && example >= mo->address)
		return true;

	mo = NULL;
	c_addr = ConstantExpr::create(~0ULL, address->getWidth());
	return true;
}

bool AddressSpace::getFeasibleObject(
	ExecutionState &state,
	TimingSolver *solver,
	ref<Expr> address,
	ObjectPair& res)
{
	bool			found, ok;
	ref<ConstantExpr>	c_addr;

	res.first = NULL;

	if (ConstantExpr * CE = dyn_cast<ConstantExpr > (address)) {
		found = resolveOne(CE, res);
		if (!found) res.first = NULL;
		return true;
	}

	TimerStatIncrementer timer(stats::resolveTime);

	ok = testInBoundPointer(state, solver, address, c_addr, res.first);
	if (!ok) return false;

	if (res.first != NULL) {
		/* we lucked out and found a feasible address. */
		if (resolveOne(c_addr, res))
			return true;
	}

	// We couldn't throw a dart and hit a feasible address.
	// The next step is to try to find any feasible address.
	MemoryObject toFind(c_addr->getZExtValue());
	MMIter oi = objects.upper_bound(&toFind);
	MMIter gt = oi;
	if (gt == objects.end())
		--gt;
	else
		++gt;

	MMIter lt = oi;
	if (lt != objects.begin()) --lt;

	MMIter begin = objects.begin();
	MMIter end = objects.end();
	--end;

	std::pair<MMIter, MMIter>
		left(objects.begin(),lt),
		right(gt,objects.end());
	while (true) {
		// Check whether current range of MemoryObjects is feasible
		unsigned i = 0;
		for (; i < 2; i++) {
			std::pair<MMIter, MMIter> cur = i ? right : left;
			const MemoryObject *low;
			const MemoryObject *high;

			if (cur.first == objects.end() ||
			    cur.second == objects.end())
				continue;


			low = cur.first->first;
			high = cur.second->first;

			if (!isFeasibleRange(state, solver, address, low, high)) {
				/* address can not possibly be in
				 * this range */
				continue;
			}

			// range is feasible-- address may
			// it only contains one MemoryObject (proven feasible),
			// so return it
			if (low == high) {
				res = *cur.first;
				return true;
			}

			// feasible range contains >1 object,
			// divide in half and continue search

			// find the midpoint between ei and bi
			MMIter mid = getMidPoint(cur.first, cur.second);

			left = std::make_pair(cur.first, mid);
			right = std::make_pair(++mid, cur.second);
			break; // out of for loop
		}

		// neither range was feasible
		if (i == 2) {
			res.first = NULL;
			return true;
		}
	}

	res.first = NULL;
	return true;
}

/* ret true => incomplete, ret false => OK */
// XXX in general this isn't exactly what we want... for
// a multiple resolution case (or for example, a \in {b,c,0})
// we want to find the first object, find a cex assuming
// not the first, find a cex assuming not the second...
// etc.

// XXX how do we smartly amortize the cost of checking to
// see if we need to keep searching up/down, in bad cases?
// maybe we don't care?

// XXX we really just need a smart place to start (although
// if its a known solution then the code below is guaranteed
// to hit the fast path with exactly 2 queries). we could also
// just get this by inspection of the expr.

// DAR: replaced original O(N) lookup with O(log N) binary search strategy

// Iteratively divide set of MemoryObjects into halves and check whether
// any feasible address exists in each range. If so, continue iterating.
// Otherwise, abandon that range of addresses.
bool AddressSpace::resolve(
	ExecutionState &state,
	TimingSolver *solver,
	ref<Expr> p,
	ResolutionList &rl,
	unsigned maxResolutions)
{
	/* fast path for constant expressions */
	if (ConstantExpr * CE = dyn_cast<ConstantExpr > (p)) {
		ObjectPair res;
		if (resolveOne(CE, res))
			rl.push_back(res);
		return false;
	}

	TimerStatIncrementer timer(stats::resolveTime);

	ref<ConstantExpr> cex;
	if (!solver->getValue(state, p, cex))
		return true;

	MemoryObject toFind(cex->getZExtValue() /* example */);

	MMIter	oi = objects.find(&toFind);
	MMIter	gt = oi;
	if (gt != objects.end())
		++gt;

	MMIter lt = oi;
	if (lt != objects.begin())
		--lt;

	MMIter end = objects.end();
	--end;

	// Explicit stack to avoid recursion
	std::stack < std::pair<typeof (oi), typeof (oi)> > tryRanges;

	// Search [begin, first object < example] if range is not empty
	if (lt != objects.begin())
		tryRanges.push(std::make_pair(objects.begin(), lt));

	// Search [first object > example, end-1] if range is not empty
	if (gt != objects.end())
		tryRanges.push(std::make_pair(gt, end));

	// Search [example,example] if exists (may not on weird overlap cases)
	// NOTE: check the example first in case of fast path,
	// so push onto stack last
	if (oi != objects.end())
		tryRanges.push(std::make_pair(oi, oi));

	return binsearchRange(
		state, p, solver, tryRanges, maxResolutions, rl);
}

/* true => incomplete,
 * false => complete
 */
bool AddressSpace::binsearchRange(
	ExecutionState& state,
	ref<Expr>	p,
	TimingSolver *solver,
	std::stack<std::pair<MMIter, MMIter> >& tryRanges,
	unsigned int maxResolutions,
	ResolutionList& rl)
{
	// Iteratively perform binary search until stack is empty
	while (!tryRanges.empty()) {
		MMIter bi = tryRanges.top().first;
		MMIter ei = tryRanges.top().second;
		const MemoryObject *low = bi->first;
		const MemoryObject *high = ei->first;

		tryRanges.pop();

		// Check whether current range of MemoryObjects is feasible
		if (!isFeasibleRange(state, solver, p, low, high))
			continue; // XXX return true on query error

		if (low != high) {
			// range contains more than one object,
			// so divide in half and push halves onto stack

			// find the midpoint between ei and bi
			MMIter mid = getMidPoint(bi, ei);
			tryRanges.push(std::make_pair(bi, mid));
			tryRanges.push(std::make_pair(++mid, ei));
			continue;
		}

		// range is feasible
		// range only contains one MemoryObject,
		// which was proven feasible, so add to resolution list
		rl.push_back(*bi);

		// fast path check
		unsigned size = rl.size();
		if (size == 1) {
			bool mustBeTrue;
			if (!solver->mustBeTrue(
				state,
				getFeasibilityExpr(p, low, high),
				mustBeTrue))
			{
				return true;
			}

			if (mustBeTrue)
				return false;
		}

		if (size == maxResolutions)
			return true;
	}

	return false;
}

// These two are pretty big hack so we can sort of pass memory back
// and forth to externals. They work by abusing the concrete cache
// store inside of the object states, which allows them to
// transparently avoid screwing up symbolics (if the byte is symbolic
// then its concrete cache byte isn't being used) but is just a hack.
void AddressSpace::copyOutConcretes(void)
{
	foreach (it, objects.begin(), objects.end()) {
		const MemoryObject	*mo = it->first;
		ObjectState		*os;
		uint8_t			*address;

		if (mo->isUserSpecified) continue;

		os = it->second;
		address = (uint8_t*) (uintptr_t) mo->address;

		if (!os->readOnly)
			memcpy(address, os->concreteStore, mo->size);
	}
}

bool AddressSpace::copyToBuf(const MemoryObject* mo, void* buf) const
{
	return copyToBuf(mo, buf, (unsigned)0, (unsigned)mo->size);
}

bool AddressSpace::copyToBuf(
	const MemoryObject* mo, void* buf,
	unsigned off, unsigned len) const
{
	const ObjectState* os;

	os = findObject(mo);
	if (os == NULL)
		return false;

	assert (len <= (mo->size - off) && "LEN+OFF SPANS >1 MO");
	memcpy(buf, os->concreteStore + off, len);
	return true;
}

bool AddressSpace::copyInConcretes(void)
{
  foreach (it, objects.begin(), objects.end()) {
    const MemoryObject *mo;
    const ObjectState *os;
    ObjectState *wos;
    uint8_t *address;

    mo = it->first;
    if (mo->isUserSpecified) continue;

    os = it->second;
    address = (uint8_t*) (uintptr_t) mo->address;

    if (memcmp(
          address,
          os->concreteStore,
          mo->size) == 0) continue;

    wos = getWriteable(mo, os);
    memcpy(wos->concreteStore, address, mo->size);
  }

  return true;
}

void AddressSpace::print(std::ostream& os) const
{
	foreach (it, objects.begin(), objects.end()) {
		it->first->print(os);
		os << std::endl;
	}
}

/***/

bool MemoryObjectLT::operator()(
	const MemoryObject *a, const MemoryObject *b) const
{
	assert (a && b);
	return a->address < b->address;
}

//===-- AddressSpace.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <llvm/Support/CommandLine.h>
#include "klee/ExecutionState.h"
#include "AddressSpace.h"
#include "CoreStats.h"
#include "Memory.h"
#include "StateSolver.h"
#include "klee/Expr.h"
#include "klee/TimerStatIncrementer.h"
#include "klee/Internal/ADT/RNG.h"
#include <stdint.h>
#include <stack>
#include "static/Sugar.h"

using namespace llvm;
using namespace klee;

namespace klee  { extern RNG theRNG; }

namespace {
	cl::opt<bool>
	ContiguousOffsetResolution(
		"contig-off-resolution",
		cl::desc("Scan only contiguous offsets on ptr resolution."),
		cl::init(false));

	cl::opt<bool> RandomizePtrAddend(
		"randomize-ptraddend",
		cl::desc("Randomize pointer addend hint for ptr+u8."),
		cl::init(false));
}


static unsigned	ContiguousPrevScanLen = 10;
static unsigned	ContiguousNextScanLen = 20;

void AddressSpace::unbindObject(const MemoryObject *mo)
{
	if (mo == last_mo)
		last_mo = NULL;

	objects = objects.remove(mo);
	generation++;
}

const ObjectState *AddressSpace::findObject(const MemoryObject *mo) const
{
	const MemoryMap::value_type *res = objects.lookup(mo);
	return res ? res->second :  NULL;
}

void AddressSpace::bindObject(const MemoryObject *mo, ObjectState *os)
{
	if (os->isZeroPage() == false) {
		assert(	!os->hasOwner() && "object already has owner");
		os->setOwner(cowKey);
	}

	objects = objects.replace(std::make_pair(mo, os));
	generation++;

	if (mo == last_mo)
		last_mo = NULL;
}

ObjectState *AddressSpace::getWriteable(
	const MemoryObject *mo,
	const ObjectState *os)
{
	ObjectState	*n;

	assert(!os->readOnly);

	if (os->isOwner(cowKey))
		return const_cast<ObjectState*> (os);

	n = ObjectState::create(*os);
	n->setOwner(cowKey);
	objects = objects.replace(std::make_pair(mo, n));
	generation++;

	return n;
}

ObjectState* AddressSpace::findWriteableObject(const MemoryObject* mo)
{
	const ObjectState*	ros;

	ros = findObject(mo);
	if (ros == NULL)
		return NULL;

	return getWriteable(mo, ros);
}

bool AddressSpace::resolveOne(uint64_t address, ObjectPair &result)
{
	const MemoryMap::value_type 	*res;
	const MemoryObject		*mo;

	if (address == 0)
		return false;

	MemoryObject			toFind(address);

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
		MK_UGE(address, lo->getBaseExpr()),
		MK_ULT(	address,
			MK_ADD(hi->getBaseExpr(), hi->getSizeExpr())));
	return inRange;
}

bool AddressSpace::isFeasibleRange(
	ExecutionState &state,
	StateSolver *solver,
	ref<Expr> address,
	const MemoryObject* lo, const MemoryObject* hi,
	bool& ok)
{
	bool		mayBeTrue;
	ref<Expr>	inRange = getFeasibilityExpr(address, lo, hi);

	ok = solver->mayBeTrue(state, inRange, mayBeTrue);
	if (!ok) return false;

	return mayBeTrue;
}

// try cheap search, will succeed for *any* inbounds pointer
// success => mo != NULL
bool AddressSpace::testInBoundPointer(
	ExecutionState		&state,
	StateSolver		*solver,
	ref<Expr>		address,
	ref<ConstantExpr>	&c_addr,
	const MemoryObject*	&mo)
{
	uint64_t			example = 0;

	mo = NULL;

	if (	isa<AddExpr>(address) &&
		isa<ConstantExpr>(address->getKid(0)) &&
		isa<ZExtExpr>(address->getKid(1)))
	{
		/* handle case (add base_constant (zext w8)) */
		const ZExtExpr		*ze;

		ze = cast<ZExtExpr>(address->getKid(1));
		if (ze->getKid(0)->getWidth() == 8) {
			c_addr = cast<ConstantExpr>(address->getKid(0));
			example = c_addr->getZExtValue();
			if (RandomizePtrAddend)
				example += theRNG.getInt31() % 256;
		}
	}

#if 1
	if (	example == 0 &&
		isa<ConstantExpr>(address->getKid(0)) &&
		isa<ZExtExpr>(address->getKid(1)) &&
		address->getKid(0)->getWidth() >= 10)
	{
		/* try to go out of range */
		example = cast<ConstantExpr>(address->getKid(0))->getZExtValue();
		example += 1 << (address->getKid(0)->getWidth() - 1);
	}
#endif

	if (example != 0) {
		if (lookupGuess(example, mo))
			return true;
	}

	if (solver->getValue(state, address, c_addr) == false) {
		c_addr = ConstantExpr::create(~0ULL, address->getWidth());
		return false;
	}

	example = c_addr->getZExtValue();
	if (lookupGuess(example, mo) == false)
		c_addr = ConstantExpr::create(~0ULL, address->getWidth());

	return true;
}

bool AddressSpace::lookupGuess(
	uint64_t		example,
	const MemoryObject*	&mo)
{
	const MemoryMap::value_type	*res;
	MemoryObject toFind(example);

	mo = NULL;
	res = objects.lookup_previous(&toFind);
	if (res == NULL)
		return false;

	mo = res->first;
	if (example < mo->address + mo->size && example >= mo->address)
		return true;

	mo = NULL;
	return false;
}

bool AddressSpace::getFeasibleObject(
	ExecutionState &state,
	StateSolver *solver,
	ref<Expr> address,
	ObjectPair& res)
{
	ref<ConstantExpr>	c_addr;
	TimerStatIncrementer	timer(stats::resolveTime);

	res.first = NULL;

	if (ConstantExpr * CE = dyn_cast<ConstantExpr > (address))
		return resolveOne(CE, res);

	if (!testInBoundPointer(state, solver, address, c_addr, res.first))
		return false;

	if (res.first != NULL) {
		/* we lucked out and found a maybe-feasible address. */
		if (resolveOne(c_addr, res))
			return true;
	}

	// We couldn't throw a dart and hit a feasible address.
	// The next step is to try to find any feasible address.
	return binsearchFeasible(
		state, solver, address, c_addr->getZExtValue(), res);
}

bool AddressSpace::binsearchFeasible(
	ExecutionState& state,
	StateSolver* solver,
	ref<Expr>& addr,
	uint64_t upper_addr, ObjectPair& res)
{
	MemoryObject	toFind(upper_addr);
	MMIter		oi = objects.upper_bound(&toFind);
	MMIter		gt = oi;
	MMIter		lt = oi;

	if (gt == objects.end())
		--gt;
	else
		++gt;

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
			bool ok, feasible;
			std::pair<MMIter, MMIter> cur = i ? right : left;
			const MemoryObject *low;
			const MemoryObject *high;

			if (cur.first == objects.end() ||
			    cur.second == objects.end())
				continue;


			low = cur.first->first;
			high = cur.second->first;

			feasible = isFeasibleRange(
				state, solver, addr, low, high, ok);

			if (!ok) {
				res.first = NULL;
				return false;
			}

			/* address can not possibly be in this range? */
			if (feasible == false)
				continue;

			// range is feasible-- 
			// address may only contains one MemoryObject
			// (proven feasible), so return it
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
			break;
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

// DAR: replaced original O(N) lookup with O(log N) binary search strategy

// Iteratively divide set of MemoryObjects into halves and check whether
// any feasible address exists in each range. If so, continue iterating.
// Otherwise, abandon that range of addresses.
bool AddressSpace::resolve(
	ExecutionState &state,
	StateSolver *solver,
	ref<Expr> p,
	ResolutionList &rl,
	unsigned maxResolutions)
{
	TimerStatIncrementer timer(stats::resolveTime);

	/* fast path for constant expressions */
	if (ConstantExpr * CE = dyn_cast<ConstantExpr > (p)) {
		ObjectPair res;
		if (resolveOne(CE, res))
			rl.push_back(res);
		return false;
	}

	if (ContiguousOffsetResolution) {
		bool	bad_addr;
		if (contigOffsetSearchRange(state, p, solver, rl, bad_addr))
			return bad_addr;
	}

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


MMIter AddressSpace::lower_bound(uint64_t addr) const
{
	if (addr == 0) return begin();
	if (objects.empty()) return end();

	MemoryObject	toFind(addr);
	return objects.lower_bound(&toFind);
}

bool AddressSpace::mustContain(
	ExecutionState &state,
	StateSolver* solver,
	ref<Expr> address,
	const MemoryObject* lo,
	const MemoryObject* hi,
	bool& ok)
{
	bool		mustBeTrue;
	ref<Expr>	feas(getFeasibilityExpr(address, lo, hi));

	ok = solver->mustBeTrue(state, feas, mustBeTrue);

	if (!ok) return false;

	return mustBeTrue;
}

/**
 * If the address takes the form
 * (add <some pointer> (some nasty expresssion))
 * we can cheat and consider this to be a pointer within
 * the contiguous neighborhood of <some pointer>
 */
bool AddressSpace::contigOffsetSearchRange(
	ExecutionState& state,
	ref<Expr>	p,
	StateSolver *solver,
	ResolutionList& rl,
	bool &bad_addr)
{
	AddExpr			*ae = dyn_cast<AddExpr>(p);
	ConstantExpr		*ce;
	ObjectPair		cur_obj;
	const MemoryObject	*mo_lo, *mo_hi;
	uint64_t		base_ptr;
	bool			ok, partial_seg;
	unsigned int		prev_objs, next_objs;

	bad_addr = false;

	if (ae == NULL)
		return false;

	ce = dyn_cast<ConstantExpr>(ae->left);
	if (ce == NULL || ce->getWidth() != 64)
		return false;

	/* confirm pointer */
	base_ptr = ce->getZExtValue();
	if (!resolveOne(base_ptr, cur_obj))
		return false;

	if (isFeasible(state, solver, p, cur_obj.first, ok) == false)
		return false;

	if (!ok) return false;

	/* pointer is OK; scan contiguous regions */
	rl.push_back(cur_obj);

	/* fast path check */
	if (mustContain(state, solver, p, cur_obj.first, ok))
		return true;

	if (!ok) return false;

	mo_hi = cur_obj.first;
	mo_lo = cur_obj.first;
	partial_seg = false;

	/* scan backwards */
	prev_objs = 0;
	do {
		uint64_t	next_base = mo_lo->address - 1;

		if (!resolveOne(next_base, cur_obj))
			break;

		if (prev_objs < ContiguousPrevScanLen) {
			if (isFeasible(state, solver, p, cur_obj.first, ok)) {
				rl.push_back(cur_obj);
			} else {
				/* doesn't cover entire contig region */
				partial_seg = true;
				break;
			}
		}

		prev_objs++;
		mo_lo = cur_obj.first;
	} while (1);

	if (prev_objs >= ContiguousPrevScanLen) {
		klee_warning("Skipped %d objects in fast scan", prev_objs);
	}

	/* scan forward */
	next_objs = 0;
	do {
		uint64_t	next_base = mo_hi->address + mo_hi->size;

		if (!resolveOne(next_base, cur_obj))
			break;

		if (next_objs < ContiguousNextScanLen) {
			if (isFeasible(state, solver, p, cur_obj.first, ok)) {
				rl.push_back(cur_obj);
			} else {
				/* doesn't cover entire contig region */
				partial_seg = true;
				break;
			}
		}

		next_objs++;
		mo_hi = cur_obj.first;
	} while (1);

	if (next_objs >= ContiguousNextScanLen) {
		klee_warning("Skipped %d next objects in fast scan", next_objs);
	}


	if (mustContain(state, solver, p, mo_lo, mo_hi, ok))
		return true;

	if (ok == false || partial_seg == true) {
		/* ulp, partially covers a segment and falls outside the
		 * partial coverage. We'll need a full scan. Ack! */
		rl.clear();
		return false;
	}


	/* covers an entire segment and more,
	 * this pointer has the magic touch */
	bad_addr = true;
	return true;
}


/* true => incomplete,
 * false => complete
 */
bool AddressSpace::binsearchRange(
	ExecutionState& state,
	ref<Expr>	p,
	StateSolver *solver,
	std::stack<std::pair<MMIter, MMIter> >& tryRanges,
	unsigned int maxResolutions,
	ResolutionList& rl)
{
	// Iteratively perform binary search until stack is empty
	while (!tryRanges.empty()) {
		bool	ok, feasible;
		MMIter bi = tryRanges.top().first;
		MMIter ei = tryRanges.top().second;
		const MemoryObject *low = bi->first;
		const MemoryObject *high = ei->first;

		tryRanges.pop();

		// Check whether current range of MemoryObjects is feasible
		feasible = isFeasibleRange(state, solver, p, low, high, ok);
		if (!ok) return true;
		if (!feasible) {
			continue;
		}

		if (low != high) {
			// range contains more than one object,
			// so divide in half and push halves onto stack

			// find the midpoint between ei and bi
			MMIter mid = getMidPoint(bi, ei);
			std::pair<MMIter, MMIter>	lo_part(bi, mid);
			std::pair<MMIter, MMIter>	hi_part(++mid, ei);

			tryRanges.push(hi_part);

			// expand lower half of address range first
			tryRanges.push(lo_part);
			continue;
		}

		// range is feasible
		// range only contains one MemoryObject,
		// which was proven feasible, so add to resolution list
		rl.push_back(*bi);

		// fast path check
		unsigned size = rl.size();
		if (size == 1) {
			bool r = mustContain(state, solver, p, low, ok);
			if (!ok) return true;
			if (r) return false;
		}

		if (size == maxResolutions) {
			klee_warning("Hit maximum resolution count");
			return true;
		}
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

		if (!os->readOnly) os->readConcrete(address, mo->size);
	}
}

void AddressSpace::copyToExprBuf(
	const MemoryObject* mo, ref<Expr>* buf,
	unsigned off, unsigned len) const
{
	const ObjectState	*os;

	os = findObject(mo);
	assert (os != NULL && "ObjectState not found, but expected!?");
	for (unsigned int i = 0; i < len; i++) {
		buf[i] = os->read8(off + i);
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
	os->readConcrete((uint8_t*)buf, len, off);
	return true;
}

bool AddressSpace::copyInConcretes(void)
{
	foreach (it, objects.begin(), objects.end()) {
		const MemoryObject	*mo;
		const ObjectState	*os;
		ObjectState		*wos;
		uint8_t			*address;

		mo = it->first;
		if (mo->isUserSpecified)
			continue;

		os = it->second;
		address = (uint8_t*)((uintptr_t) mo->address);

		if (os->cmpConcrete(address, mo->size) == 0)
			continue;

		wos = getWriteable(mo, os);
		wos->writeConcrete(address, mo->size);
	}

	return true;
}

void AddressSpace::print(std::ostream& os) const
{
	foreach (it, objects.begin(), objects.end()) {
		const ObjectState	*ros = it->second;
		it->first->print(os);
		os << ". hash=" << ros->hash() << '\n';
	}
}

bool MemoryObjectLT::operator()(
	const MemoryObject *a, const MemoryObject *b) const
{
	assert (a && b);
	return a->address < b->address;
}

void AddressSpace::printAddressInfo(std::ostream& info, uint64_t addr) const
{
	MemoryObject		hack((unsigned) addr);
	MemoryMap::iterator	lower(objects.upper_bound(&hack));

	info << "\tnext: ";
	if (lower == objects.end()) {
		info << "none\n";
	} else {
		const MemoryObject	*mo = lower->first;
		std::string		alloc_info;

		mo->getAllocInfo(alloc_info);
		info	<< "object at "	<< (void*)mo->address
			<< " of size "	<< mo->size
			<< "\n\t\t"
			<< alloc_info << "\n";
	}

	if (lower == objects.begin())
		return;

	--lower;
	info << "\tprev: ";
	if (lower == objects.end()) {
		info << "none\n";
	} else {
		const MemoryObject *mo = lower->first;
		std::string alloc_info;
		mo->getAllocInfo(alloc_info);
		info << "object at " << (void*)mo->address
			<< " of size " << mo->size << "\n"
			<< "\t\t" << alloc_info << "\n";
	}
}

void AddressSpace::printObjects(std::ostream& os) const { os << objects; }

unsigned AddressSpace::hash(void) const
{
	unsigned hash_ret = 0;

	foreach (it, objects.begin(), objects.end()) {
		const MemoryObject	*mo = it->first;
		const ObjectState	*os = it->second;

		hash_ret ^= (mo->address + os->hash());
	}

	return hash_ret;
}

unsigned int AddressSpace::copyOutBuf(
	uint64_t	addr,
	const char	*bytes,
	unsigned int	len)
{
	unsigned int	bw = 0;

	while (bw < len) {
		const MemoryObject	*mo;
		ObjectState		*os;
		unsigned int		to_write;
		unsigned int		bytes_avail, off;

		mo = resolveOneMO(addr + bw);
		if (mo == NULL)
			break;

		os = findWriteableObject(mo);
		if (os == NULL)
			break;

		off = (addr+bw)-mo->address;
		bytes_avail = mo->size - off;

		to_write = (bytes_avail < (len - bw))
			? bytes_avail
			: len-bw;

		for (unsigned i = 0; i < to_write; i++)
			os->write8(off+i, bytes[bw+i]);

		bw += to_write;
	}

	return bw;
}

ref<Expr> AddressSpace::getOOBCond(ref<Expr>& symptr) const
{
	ref<Expr>	ret_expr = ConstantExpr::create(0,1);
	uint64_t	extent_begin, extent_len;

	extent_begin = 0;
	extent_len = 0;
	foreach (it, begin(), end()) {
		const MemoryObject	*mo;

		mo = it->first;
		if (mo->address == (extent_begin + extent_len)) {
			extent_len += mo->size;
			continue;
		}

		if (extent_len != 0) {
			ref<Expr>	bound_chk;
			bound_chk = UltExpr::create(
				SubExpr::create(
					symptr,
					ConstantExpr::create(extent_begin, 64)),
				ConstantExpr::create(extent_len, 64));

			ret_expr = OrExpr::create(ret_expr, bound_chk);
		}

		extent_begin = mo->address;
		extent_len = mo->size;
	}

	if (extent_len != 0) {
		ref<Expr>	bound_chk;
		bound_chk = UltExpr::create(
			SubExpr::create(
				symptr,
				ConstantExpr::create(extent_begin, 64)),
			ConstantExpr::create(extent_len, 64));

		ret_expr = OrExpr::create(ret_expr, bound_chk);
	}

	return NotExpr::create(ret_expr);
}

std::vector<std::pair<void*, unsigned> > AddressSpace::getMagicExtents(void)
{
	std::vector<std::pair<void*, unsigned> >	ret;
	std::pair<void*, unsigned>		cur_ext;

	cur_ext.first = NULL;
	cur_ext.second = 0;
	foreach (it, begin(), end()) {
		const MemoryObject	*mo = it->first;
		const ObjectState	*os = it->second;
		void			*ext_base;

		ext_base = ((char*)cur_ext.first + cur_ext.second);

		if (ext_base != (void*)mo->address) {
			if (cur_ext.second > 16) {
				cur_ext.second /= 16;
				cur_ext.second *= 16;
				if (cur_ext.second)
					ret.push_back(cur_ext);
			}

			cur_ext.first = NULL;
			cur_ext.second = 0;
		}

		for (unsigned i = 0; i < mo->size; i++) {
			uint8_t	c;

			/* mismatch */
			if (	os->isByteConcrete(i) == false ||
				((c = os->read8c(i)) != 0xa3))
			{
				if (cur_ext.first == NULL)
					continue;

				if (cur_ext.second > 16) {
					cur_ext.second /= 16;
					cur_ext.second *= 16;
					if (cur_ext.second)
						ret.push_back(cur_ext);
				}

				cur_ext.first = NULL;
				cur_ext.second = 0;
				continue;
			}

			/* match */
			if (cur_ext.first == NULL) {
				/* start run */
				cur_ext.first = (char*)mo->address + i;
				cur_ext.second = 0;
			}

			/* new byte matched */
			cur_ext.second++;
		}
	}

	return ret;
}

bool AddressSpace::readConcrete(
	std::vector<uint8_t>& v,
	std::vector<bool>& is_conc,
	uint64_t addr,
	unsigned len) const
{
	MemoryObject	hack(addr);
	MMIter		it = objects.upper_bound(&hack), e = end();
	uint64_t	cur_addr;
	unsigned	remaining;
	bool		bogus_reads = false;

	if (it != begin() && it->first->address > addr)
		--it;

	cur_addr = addr;
	remaining = len;

	while (it != e) {
		const MemoryObject*	mo;
		const ObjectState*	os;
		unsigned		mo_off, v_off, i;

		mo = it->first;
		/* offset into mo to get cur_addr */
		mo_off = (mo->address < cur_addr)
			? cur_addr - mo->address
			: 0;
		if (mo_off >= mo->size) {
			++it;
			continue;
		}

		v_off = (mo->address > cur_addr)
			? mo->address - cur_addr
			: 0;

		/* gggMMMMM; gap in memory; fill in */
		if (v_off) {
			i = 0;
			bogus_reads = true;
			while (i < v_off && remaining) {
				v.push_back(0);
				is_conc.push_back(false);
				remaining--;
				i++;
			}
		}

		if (!remaining) return bogus_reads;

		os = it->second;
		i = 0;
		while (i < (mo->size - mo_off) && remaining) {
			if (os->isByteConcrete(i + mo_off)) {
				v.push_back(os->read8c(i + mo_off));
				is_conc.push_back(true);
			} else {
				v.push_back(0);
				is_conc.push_back(false);
				bogus_reads = true;
			}
			i++;
			remaining--;
		}

		if (!remaining) return bogus_reads;
	}

	return bogus_reads;
}

void AddressSpace::clear(void)
{
	last_mo = NULL;
	cowKey = -1;
	generation = 0;
	objects = MemoryMap();
}

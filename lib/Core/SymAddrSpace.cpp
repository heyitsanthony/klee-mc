#include <llvm/Support/CommandLine.h>

#include "CoreStats.h"
#include "klee/TimerStatIncrementer.h"
#include "klee/Internal/ADT/RNG.h"
#include "klee/ExecutionState.h"
#include "StateSolver.h"
#include "static/Sugar.h"
#include "SymAddrSpace.h"

using namespace llvm;
using namespace klee;

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

namespace klee  { extern RNG theRNG; }

static unsigned	ContiguousPrevScanLen = 10;
static unsigned	ContiguousNextScanLen = 20;

bool SymAddrSpace::getFeasibleObject(
	ExecutionState &es,
	StateSolver *solver,
	ref<Expr> address,
	ObjectPair &result)
{
	SymAddrSpace	sas(solver, es);
	return sas.getFeasibleObject(address, result);
}

/// Resolve address to a list of ObjectPairs it can point to. If
/// maxResolutions is non-zero then no more than that many pairs
/// will be returned.
///
/// \return true iff the resolution is incomplete (maxResolutions
/// is non-zero and the search terminated early, or a query timed out).
bool SymAddrSpace::resolve(
	ExecutionState &es,
	StateSolver *solver,
	ref<Expr> address,
	ResolutionList &rl,
	unsigned maxResolutions)
{
	SymAddrSpace	sas(solver, es);
	return sas.resolve(address, rl, maxResolutions);
}

ref<Expr> SymAddrSpace::getOOBCond(const AddressSpace& as, ref<Expr>& symptr)
{
	ref<Expr>	ret_expr = ConstantExpr::create(0,1);
	uint64_t	extent_begin, extent_len;

	extent_begin = 0;
	extent_len = 0;
	foreach (it, as.begin(), as.end()) {
		const MemoryObject	*mo;

		mo = it->first;
		if (mo->address == (extent_begin + extent_len)) {
			extent_len += mo->size;
			continue;
		}

		if (extent_len != 0) {
			ref<Expr>	bound_chk;
			bound_chk = UltExpr::create(
				MK_SUB(	symptr,
					MK_CONST(extent_begin, 64)),
				MK_CONST(extent_len, 64));

			ret_expr = MK_OR(ret_expr, bound_chk);
		}

		extent_begin = mo->address;
		extent_len = mo->size;
	}

	if (extent_len != 0) {
		ref<Expr>	bound_chk;
		bound_chk = UltExpr::create(
			MK_SUB(symptr, MK_CONST(extent_begin, 64)),
			MK_CONST(extent_len, 64));

		ret_expr = MK_OR(ret_expr, bound_chk);
	}

	return MK_NOT(ret_expr);
}

bool SymAddrSpace::mustContain(
	ref<Expr> address,
	const MemoryObject* lo,
	const MemoryObject* hi,
	bool& ok)
{
	bool		mustBeTrue;
	ref<Expr>	feas(getFeasibilityExpr(address, lo, hi));

	ok = solver->mustBeTrue(es, feas, mustBeTrue);

	if (!ok) return false;

	return mustBeTrue;
}

ref<Expr> SymAddrSpace::getFeasibilityExpr(
	ref<Expr> address,
	const MemoryObject* lo,
	const MemoryObject* hi) const
{
	/* address >= low->base &&
	 * address < high->base+high->size) */
	ref<Expr> inRange =
	MK_AND(	MK_UGE(address, lo->getBaseExpr()),
		MK_ULT(	address,
			MK_ADD(hi->getBaseExpr(), hi->getSizeExpr())));
	return inRange;
}

/////////////
bool SymAddrSpace::isFeasibleRange(
	ref<Expr> address,
	const MemoryObject* lo, const MemoryObject* hi,
	bool& ok)
{
	bool		mayBeTrue;
	ref<Expr>	inRange = getFeasibilityExpr(address, lo, hi);

	ok = solver->mayBeTrue(es, inRange, mayBeTrue);
	if (!ok) return false;

	return mayBeTrue;
}

// try cheap search, will succeed for *any* inbounds pointer
// success => mo != NULL
bool SymAddrSpace::testInBoundPointer(
	ref<Expr>		address,
	ref<ConstantExpr>	&c_addr,
	const MemoryObject*	&mo)
{
	uint64_t	example = 0;

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
		if (es.addressSpace.lookupGuess(example, mo))
			return true;
	}

	if (solver->getValue(es, address, c_addr) == false) {
		c_addr = ConstantExpr::create(~0ULL, address->getWidth());
		return false;
	}

	example = c_addr->getZExtValue();
	if (es.addressSpace.lookupGuess(example, mo) == false)
		c_addr = ConstantExpr::create(~0ULL, address->getWidth());

	return true;
}


bool SymAddrSpace::getFeasibleObject(ref<Expr>& address, ObjectPair& res)
{
	ref<ConstantExpr>	c_addr;
	TimerStatIncrementer	timer(stats::resolveTime);

	res.first = NULL;

	if (ConstantExpr * CE = dyn_cast<ConstantExpr > (address))
		return es.addressSpace.resolveOne(CE->getZExtValue(), res);

	if (!testInBoundPointer(address, c_addr, res.first))
		return false;

	if (res.first != NULL) {
		/* we lucked out and found a maybe-feasible address. */
		if (es.addressSpace.resolveOne(c_addr->getZExtValue(), res))
			return true;
	}

	// We couldn't throw a dart and hit a feasible address.
	// The next step is to try to find any feasible address.
	return binsearchFeasible(address, c_addr->getZExtValue(), res);
}

bool SymAddrSpace::binsearchFeasible(
	ref<Expr>& addr, uint64_t upper_addr, ObjectPair& res)
{
	MemoryObject	toFind(upper_addr);
	MMIter		oi = es.addressSpace.objects.upper_bound(&toFind);
	MMIter		gt = oi;
	MMIter		lt = oi;

	if (gt == es.addressSpace.objects.end())
		--gt;
	else
		++gt;

	if (lt != es.addressSpace.objects.begin()) --lt;

	MMIter begin = es.addressSpace.objects.begin();
	MMIter end = es.addressSpace.objects.end();
	--end;

	std::pair<MMIter, MMIter>
		left(es.addressSpace.objects.begin(),lt),
		right(gt,es.addressSpace.objects.end());

	while (true) {
		// Check whether current range of MemoryObjects is feasible
		unsigned i = 0;
		for (; i < 2; i++) {
			bool ok, feasible;
			std::pair<MMIter, MMIter> cur = i ? right : left;
			const MemoryObject *low;
			const MemoryObject *high;

			if (cur.first == es.addressSpace.objects.end() ||
			    cur.second == es.addressSpace.objects.end())
				continue;


			low = cur.first->first;
			high = cur.second->first;

			feasible = isFeasibleRange(addr, low, high, ok);

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
			MMIter mid = es.addressSpace.getMidPoint(
				cur.first, cur.second);

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
bool SymAddrSpace::resolve(
	ref<Expr>& p,
	ResolutionList &rl,
	unsigned maxResolutions)
{
	TimerStatIncrementer timer(stats::resolveTime);

	/* fast path for constant expressions */
	if (ConstantExpr * CE = dyn_cast<ConstantExpr > (p)) {
		ObjectPair res;
		if (es.addressSpace.resolveOne(CE->getZExtValue(), res))
			rl.push_back(res);
		return false;
	}

	if (ContiguousOffsetResolution) {
		bool	bad_addr;
		if (contigOffsetSearchRange(p, rl, bad_addr))
			return bad_addr;
	}

	ref<ConstantExpr> cex;
	if (!solver->getValue(es, p, cex))
		return true;

	MemoryObject toFind(cex->getZExtValue() /* example */);

	MMIter	oi = es.addressSpace.objects.find(&toFind);
	MMIter	gt = oi;
	if (gt != es.addressSpace.objects.end())
		++gt;

	MMIter lt = oi;
	if (lt != es.addressSpace.objects.begin())
		--lt;

	MMIter end = es.addressSpace.objects.end();
	--end;

	// Explicit stack to avoid recursion
	std::stack < std::pair<decltype (oi), decltype (oi)> > tryRanges;

	// Search [begin, first object < example] if range is not empty
	if (lt != es.addressSpace.objects.begin())
		tryRanges.push(
			std::make_pair(es.addressSpace.objects.begin(), lt));

	// Search [first object > example, end-1] if range is not empty
	if (gt != es.addressSpace.objects.end())
		tryRanges.push(std::make_pair(gt, end));

	// Search [example,example] if exists (may not on weird overlap cases)
	// NOTE: check the example first in case of fast path,
	// so push onto stack last
	if (oi != es.addressSpace.objects.end())
		tryRanges.push(std::make_pair(oi, oi));

	return binsearchRange(p, tryRanges, maxResolutions, rl);
}



/**
 * If the address takes the form
 * (add <some pointer> (some nasty expresssion))
 * we can cheat and consider this to be a pointer within
 * the contiguous neighborhood of <some pointer>
 */
bool SymAddrSpace::contigOffsetSearchRange(
	ref<Expr>	p,
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
	if (!es.addressSpace.resolveOne(base_ptr, cur_obj))
		return false;

	if (isFeasible(p, cur_obj.first, ok) == false)
		return false;

	if (!ok) return false;

	/* pointer is OK; scan contiguous regions */
	rl.push_back(cur_obj);

	/* fast path check */
	if (mustContain(p, cur_obj.first, ok))
		return true;

	if (!ok) return false;

	mo_hi = cur_obj.first;
	mo_lo = cur_obj.first;
	partial_seg = false;

	/* scan backwards */
	prev_objs = 0;
	do {
		uint64_t	next_base = mo_lo->address - 1;

		if (!es.addressSpace.resolveOne(next_base, cur_obj))
			break;

		if (prev_objs < ContiguousPrevScanLen) {
			if (isFeasible(p, cur_obj.first, ok)) {
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

		if (!es.addressSpace.resolveOne(next_base, cur_obj))
			break;

		if (next_objs < ContiguousNextScanLen) {
			if (isFeasible(p, cur_obj.first, ok)) {
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

	if (mustContain(p, mo_lo, mo_hi, ok))
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
bool SymAddrSpace::binsearchRange(
	ref<Expr>	p,
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
		feasible = isFeasibleRange(p, low, high, ok);
		if (!ok) return true;
		if (!feasible) continue;

		if (low != high) {
			// range contains more than one object,
			// so divide in half and push halves onto stack

			// find the midpoint between ei and bi
			MMIter mid = es.addressSpace.getMidPoint(bi, ei);
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
			bool r = mustContain(p, low, ok);
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

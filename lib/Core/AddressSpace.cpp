//===-- AddressSpace.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AddressSpace.h"
#include "Memory.h"
#include "klee/ExecutionState.h"
#include "StateSolver.h"
#include "klee/Expr.h"
#include <stdint.h>
#include <stack>
#include "static/Sugar.h"

using namespace llvm;
using namespace klee;

void AddressSpace::unbindObject(const MemoryObject *mo)
{
	if (mo == last_mo)
		last_mo = NULL;

	objects = objects.remove(mo);
	mo_generation++;
	os_generation++;
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

	assert (os->getSize() >= mo->size);
	objects = objects.replace(std::make_pair(mo, os));
	os_generation++;

	if (mo == last_mo)
		last_mo = NULL;
}

ObjectState *AddressSpace::getWriteable(
	const MemoryObject *mo,
	const ObjectState *os)
{
	ObjectState	*n;

	assert(!os->readOnly);

	if (os->isOwner(cowKey)) return const_cast<ObjectState*> (os);

	n = ObjectState::create(*os);
	assert (n->getCopyDepth());
	n->setOwner(cowKey);
	objects = objects.replace(std::make_pair(mo, n));
	os_generation++;

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

bool AddressSpace::resolveOne(uint64_t address, ObjectPair &result) const
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

const MemoryObject* AddressSpace::resolveOneMO(uint64_t address) const
{
	ObjectPair	result;
	if (!resolveOne(address, result))
		return NULL;
	return result.first;
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

bool AddressSpace::lookupGuess(uint64_t example, const MemoryObject* &mo)
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


MMIter AddressSpace::lower_bound(uint64_t addr) const
{
	if (addr == 0) return begin();
	if (objects.empty()) return end();

	MemoryObject	toFind(addr);
	return objects.lower_bound(&toFind);
}

MMIter AddressSpace::upper_bound(uint64_t addr) const
{
	if (addr == 0) return begin();
	if (objects.empty()) return end();

	MemoryObject	toFind(addr);
	return objects.upper_bound(&toFind);
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

	assert (off + len <= mo->size);
	os = findObject(mo);

	assert (os != NULL && "ObjectState not found, but expected!?");
	assert (os->getSize() >= mo->size);
	assert (off + len <= os->getSize());

	for (unsigned int i = 0; i < len; i++) {
		buf[i] = os->read8(off + i);
	}
}

bool AddressSpace::copyToBuf(const MemoryObject* mo, void* buf) const
{ return copyToBuf(mo, buf, (unsigned)0, (unsigned)mo->size); }

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

Expr::Hash AddressSpace::hash(void) const
{
	Expr::Hash hash_ret = 0;

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

int AddressSpace::readConcreteSafe(
	uint8_t* buf, uint64_t guest_addr, unsigned len) const
{
	int		br = 0;
	int		to_copy;

	for (unsigned i = 0;  i < len; i += to_copy) {
		ObjectPair	op;
		unsigned	off, remain;
		int		copied;

		if (resolveOne(guest_addr+i, op) == false)
			break;

		off = op_mo(op)->getOffset(guest_addr+i);
		remain = len - br;
		to_copy = remain;
		if ((off + to_copy) > op_mo(op)->size)
			to_copy = op_mo(op)->size - off;

		copied = op_os(op)->readConcreteSafe(buf + i, to_copy, off);
		br += copied;

		if (copied != to_copy)
			break;
	}

	return br;
}

void AddressSpace::clear(void)
{
	last_mo = NULL;
	cowKey = -1;
	os_generation = 0;
	mo_generation = 0;
	objects = MemoryMap();
}

void AddressSpace::checkObjects(void) const
{
	bool bad = false;

	foreach (it, begin(), end()) {
		const MemoryObject	*mo(it->first);
		const ObjectState	*os(it->second);

		if (mo->size <= os->getSize()) continue;

		/* bad stuff ahead */
		bad = true;
		mo->print(std::cerr);
		std::cerr << "\nOS=" << (void*)os << ". Len=" << os->getSize() << "\n";
	}

	assert (bad == false);
}

//===-- BranchTracker.cpp ---------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "static/Sugar.h"
#include "BranchTracker.h"
#include <iostream>

#include <algorithm>
#include "MemoryManager.h"
#include "Memory.h"

using namespace klee;

BranchTracker	BranchTracker::dummyHead;

BranchTracker::BranchTracker()
: head(new Segment())
, tail(head)
, needNewSegment(false)
{}

BranchTracker::BranchTracker(const BranchTracker &a)
: head(a.head)
, tail(a.tail)
{
	if (a.empty()) {
		tail = head = new Segment();
		needNewSegment = false;
		return;
	}

	// fork new segments
	a.needNewSegment = true;
	needNewSegment = true;
}

void BranchTracker::push_back(unsigned decision, const KInstruction* ki)
{
	if (needNewSegment) {
		SegmentRef newseg = new Segment();
		tail->children.push_back(newseg.get());
		newseg->offset = tail->off() + tail->size();
		newseg->parent = tail;
		tail = newseg;
		needNewSegment = false;
	}

	tail->branchSites.push_back(ki);

	if (decision == 0 || decision == 1) {
		tail->branches.push_back(decision == 1);
		tail->isBranch.push_back(true);
		return;
	}

	tail->branches.push_back(false);
	tail->isBranch.push_back(false);
	tail->nonBranches.insert(
		std::make_pair(tail->branches.size()-1, decision));
}

bool BranchTracker::push_heap_ref(HeapObject *ho) {
  if(empty())
    return false;

  tail->heapObjects.push_back(ref<HeapObject>(ho));
  return true;
}

bool BranchTracker::empty() const
{ return (head == tail && !tail->size() && tail->children.empty()); }

size_t BranchTracker::size() const {
	size_t retVal = 0;
	for (SegmentRef it = tail; !it.isNull(); it = it->parent)
		retVal += it->size();
	return retVal;
}

BranchTracker::SegmentRef
BranchTracker::containing(unsigned index) const
{
	unsigned prefixSize = size();
	SegmentRef it = tail;
	for (; prefixSize - tail->size() > index; it = it->parent)
		prefixSize -= tail->size();
	return it.get();
}

ReplayNode BranchTracker::front() const
{
	assert(!empty());
	return (*getHead())[0];
}

ReplayNode BranchTracker::back() const
{
	assert(!empty());
	return (*tail)[tail->size()-1];
}

ReplayNode BranchTracker::operator[](unsigned index) const
{
	int		prefixSize;
	SegmentRef 	it;

	prefixSize = size();
	for (it = tail; prefixSize - tail->size() > index; it = it->parent)
		prefixSize -= tail->size();

	assert (prefixSize >= 0);
	assert (index >= (unsigned)prefixSize);

	return (*it)[index - prefixSize];
}

BranchTracker::SegmentRef BranchTracker::getHead(void) const
{
	SegmentRef	h(head);
	while (h->parent.isNull() == false) h = h->parent;
	return h;
}

BranchTracker& BranchTracker::operator=(const BranchTracker &a)
{
	head = a.getHead();
	tail = a.tail;
	needNewSegment = true;
	a.needNewSegment = true;
	return *this;
}

BranchTracker::iterator
BranchTracker::findChild(
	BranchTracker::iterator it,
        ReplayNode		branch,
        bool			&noChild) const
{
	Segment	*childSeg = NULL;

	foreach (cit, it.curSeg->children.begin(), it.curSeg->children.end()) {
		bool	isBranchMatch, isNonBranchMatch;
		Segment	*nextSeg = *cit;

		assert (!nextSeg->branches.empty());

		isBranchMatch = nextSeg->isBranch[0] &&
				nextSeg->branches[0] == branch.first;
		isNonBranchMatch =	!nextSeg->isBranch[0] &&
					nextSeg->nonBranches[0] == branch.first;

		if (isBranchMatch || isNonBranchMatch) {
		//	assert(nextSeg->branchID[0] == nextValue.second
		//	     && "Identical branch leads to different target");
			childSeg = nextSeg;
			break;
		}
	}

	if (childSeg != NULL) {
		it.curSeg = it.tail = childSeg;
		it.curSegIndex = 0;
		it.seqIndex = childSeg->off();
		noChild = false;
		return it;
	}

	++it;
	std::cerr << "HELLO NO CHILD!!\n";
	noChild = true;
	return it;
}

BranchTracker::SegmentRef
BranchTracker::insert(const ReplayPath &branches)
{
	iterator	it = begin();
	unsigned	index = 0;
	bool		noChild = false, isSplit, isOnNonEmptyHead;

	for (; it != end() && index < branches.size(); index++) {
		bool		isSegEnd;
		ReplayNode	value(branches[index]);

		// special case of initial branch having more than one target;
		// the root segment in the trie is empty.
		// this is the only time we allow an empty segment.
		if (index == 0 && it.curSeg->empty()) {
			it = findChild(it, branches[index], noChild);
			if (noChild)
				break;
			std::cerr << "OK\n";
			continue;
		}

		// we found a divergence in the branch sequences
		if ((*it).first != value.first) {
			break;
		}

		// if we're at the end of a segment,
		// then find child (if any) with matching next value
		isSegEnd = it.curSegIndex == it.curSeg->size() - 1;
		if (isSegEnd && index != branches.size() - 1) {
			it = findChild(it, branches[index+1], noChild);
			if (noChild) {
				index++;
				break;
			}
			continue;
		}

		++it;
	}

	// new set of branches is a precise subset of an existing path
	if (	noChild == false &&
		index == (it.curSeg->off() + it.curSegIndex) &&
		it.curSegIndex == it.curSeg->size())
	{
		/* XXX: might need new seg here */
		return it.curSeg;
	}

	isOnNonEmptyHead = it.curSeg != getHead() && !it.curSeg->empty();
	isSplit = noChild == false && (isOnNonEmptyHead || it.curSegIndex);

	if (isSplit) {
		// split this segment
		splitSegment(*it.curSeg, it.curSegIndex);
		it.reseat();
	}

	SegmentRef oldTail = tail;
	if (noChild || it.curSeg != getHead()) {
		SegmentRef newseg = new Segment();

		newseg->parent = noChild ? it.curSeg : it.curSeg->parent;
		newseg->parent->children.push_back(newseg.get());
		tail = it.curSeg = newseg;
		if (tail->parent.isNull() == false) {
			tail->offset = newseg->parent->off();
			tail->offset += newseg->parent->size();
		}

		newseg->offset = newseg->parent->off() + newseg->parent->size();
		needNewSegment = false;
	}

	index = tail->off();
	for (; index < branches.size(); index++) {
		push_back(branches[index]);
	}

	it = end();
	tail = oldTail;

	return it.curSeg;
}

void BranchTracker::splitSegment(Segment &seg, unsigned index)
{
	Segment	*newseg = seg.split(index);

	if (head == &seg)
		head = newseg;
}

BranchTracker::Segment* BranchTracker::Segment::split(unsigned index)
{
	Segment *newseg = new Segment();

	// newseg gets prefix, segment get suffix

	splitSwapData(index, newseg);
	splitNonBranches(index, newseg);

	newseg->offset = offset;
	offset += index;

	newseg->parent = parent;
	parent = newseg;

	if (newseg->parent.isNull() == false) {
		newseg->parent->children.erase(
			std::find(
				newseg->parent->children.begin(),
				newseg->parent->children.end(),
				this));
		newseg->parent->children.push_back(newseg);
	}

	newseg->children.push_back(this);

	return newseg;
}

void BranchTracker::Segment::splitSwapData(unsigned index, Segment* newseg)
{
	assert(index < size());

	newseg->branches.insert(
		newseg->branches.begin(),
		branches.begin(),
		branches.begin() + index);
	newseg->isBranch.insert(
		newseg->isBranch.begin(),
		isBranch.begin(),
		isBranch.begin() + index);

	newseg->branchSites.insert(
		newseg->branchSites.begin(),
		branchSites.begin(),
		branchSites.begin()+index);

	Segment::BoolVector tempBranches(
		branches.begin() + index,
		branches.end());
	Segment::BoolVector tempIsBranch(
		isBranch.begin() + index,
		isBranch.end());

	std::swap(branches, tempBranches);
	std::swap(isBranch, tempIsBranch);

	Segment::BranchSites tempBranchSites(
		branchSites.begin() + index,
		branchSites.end());
	std::swap(branchSites, tempBranchSites);

	// for safety, keep the heap object references in the parent
	std::swap(newseg->heapObjects, heapObjects);
}

void BranchTracker::Segment::splitNonBranches(unsigned index, Segment* newseg)
{
	Segment::NonBranchesTy nonBranchesTemp;

	for (	Segment::NonBranchesTy::iterator it = nonBranches.begin(),
		ie = nonBranches.end(); it != ie; )
	{
		if (it->first < index) {
			newseg->nonBranches.insert(*it);
			Segment::NonBranchesTy::iterator temp = it++;
			nonBranches.erase(temp);
			continue;
		}

		nonBranchesTemp.insert(
			std::make_pair(it->first - index, it->second));
		++it;
	}

	std::swap(nonBranchesTemp, nonBranches);
}

bool BranchTracker::iterator::mayDeref(void) const
{
	if (curSeg.isNull())
		return false;

	if (curSeg == tail && curSegIndex >= curSeg->size())
		return false;

	if (curSeg->empty())
		return false;

	return true;
}

ReplayNode BranchTracker::iterator::operator*() const
{
	assert (mayDeref());

	if (curSegIndex == curSeg->size()) {
		iterator it = *this;
		++it;
		assert (!it.curSeg->empty());

		return *it;
	}

	return (*curSeg)[curSegIndex];
}

BranchTracker::iterator BranchTracker::iterator::operator++(int notused)
{
	iterator temp = *this;
	++(*this);
	return temp;
}

void BranchTracker::iterator::bump(void)
{
	// common case
	if (curSegIndex < curSeg->size() - 1) {
		curSegIndex++;
		return;
	}

	// we're at the tail, so just advance curSegIndex;
	// if 0 children, this is equivalent to end(),
	// else fork() will handle the replay logic using getSuccessor()
	if (curSeg == tail) {
		assert(curSegIndex < curSeg->size() && "BranchTracker::it OOB");
		curSegIndex++;
		return;
	}
	assert (curSegIndex == curSeg->size()-1);

	curSegIndex = 0;

	// we're at the end of a segment with only one child,
	// so just advance to the start of the next child
	if (curSeg->children.size() == 1) {
		curSeg = curSeg->children[0];
		return;
	}

	// we're at the end of a segment other than the tail,
	// backtrack to advance to segment that lets us reach the tail
	SegmentRef temp;
	for (temp = tail; temp->parent != curSeg; temp = temp->parent);
	curSeg = temp;
}

void BranchTracker::iterator::reseat(void)
{
	if (seqIndex == curSegIndex + curSeg->off())
		return;

	/* find head */
	while (curSeg->parent.isNull() == false)
		curSeg = curSeg->parent;
	curSegIndex = 0;

	for (unsigned i = 0; i < seqIndex; i++)
		bump();

	assert (seqIndex == curSegIndex + curSeg->off());
}

BranchTracker::iterator BranchTracker::iterator::operator++()
{
	assert(!curSeg.isNull());

	reseat();
	seqIndex++;
	bump();
	return *this;
}

unsigned BranchTracker::Segment::seg_alloc_c = 0;

BranchTracker::Segment::Segment(void)
: refCount(0)
, offset(0)
{ ++seg_alloc_c; }

BranchTracker::Segment::~Segment(void)
{
	SegmentVector::iterator it;

	if (parent.isNull())
		return;

	it = std::find(parent->children.begin(), parent->children.end(), this);
	assert (it != parent->children.end());
	parent->children.erase(it);

	seg_alloc_c--;
}

ReplayNode BranchTracker::Segment::operator[](unsigned index) const
{
	assert(index < branches.size()); // bounds check
	const KInstruction	*ki;

	ki = branchSites[index];

	if (!isBranch[index]) {
		NonBranchesTy::const_iterator sit;

		sit = nonBranches.find(index);
		assert(sit != nonBranches.end());
		return ReplayNode(sit->second, ki);
	}

	return ReplayNode(branches[index], ki);
}

void BranchTracker::truncatePast(BranchTracker::iterator& it)
{
	SegmentRef	tail_trunc;

	if (it == end()) return;
	if (it.curSegIndex == it.curSeg->size()) ++it;

	/* everything past the last segment in the iterator may be discarded */
	tail = it.curSeg;

	assert (it.curSegIndex <= it.curSeg->size() - 1 && "OOB IDX");

	/* iterator ends on edge of tail, keep entire tail */
	if (it.curSegIndex == (it.curSeg->size() - 1)) {
		needNewSegment = true;
		return;
	}

	ReplayPath	rp;
	getReplayPath(rp, it);

	std::cerr << "[BT] inserting at offset=" << it.seqIndex
		<< ". Total segments: " << Segment::getNumSegments()
		<< "\n";

	tail = insert(rp);
	for (head = tail; head->parent.isNull() == false; head = head->parent);

	// assert (verifyPath(rp));
#if 0
	/* copy tail; truncate to iterator */
	assert (it.curSeg.isNull() == false);

	tail_trunc = it.curSeg->truncatePast(it.curSegIndex);
	if (head == tail) {
		head = tail_trunc;
	}

	tail = tail_trunc;
	needNewSegment = true;
#endif
}

BranchTracker::Segment* BranchTracker::Segment::truncatePast(unsigned idx)
{
	Segment	*ret = new Segment(*this);

	assert (idx < ret->branches.size());

	ret->branches.resize(idx);
	ret->isBranch.resize(idx);
	ret->branchSites.resize(idx);
	if (ret->parent.isNull() == false)
		ret->parent->children.push_back(ret);
	ret->children.clear();

	ret->nonBranches.erase(
		ret->nonBranches.upper_bound(idx),
		ret->nonBranches.end());

	return ret;
}


BranchTracker::iterator BranchTracker::begin(void) const
{return (empty()) ? end() : iterator(this); }

BranchTracker::iterator BranchTracker::end(void) const
{
	Segment *temp = tail.get();
	return iterator(this, temp, temp->size());
}

void BranchTracker::iterator::dump(void) const
{
	std::cerr << "it {\n";
	std::cerr << "curSeg = " << (void*)curSeg.get()  << '\n';
	std::cerr << "tail = " << (void*)tail.get() << '\n';
	std::cerr << "curSegOffset = " << curSeg->off() << '\n';
	std::cerr << "curSegLen = " << curSeg->size() << '\n';
	std::cerr << "curSegChildren = " << curSeg->children.size() << '\n';
	std::cerr << "curSegIdx = " << curSegIndex << '\n';
	std::cerr << "seqIdx = " << seqIndex << "}\n";
}


bool BranchTracker::verifyPath(const ReplayPath& branches)
{
	ReplayPath::const_iterator	rp_it;
	unsigned			c = 0;

	if (branches.size() != size()) {
		std::cerr	<< "[BT] Size mismatch BT=" << size()
				<< " vs RP=" << branches.size() << '\n';
		return false;
	}

	rp_it = branches.begin();
	foreach (it, begin(), end()) {
		if ((*rp_it).first != (*it).first) {
			std::cerr << "[BT] MISMATCH ON NODE: " << c << '\n';
			std::cerr << "Head: " << (void*)head.get() << '\n';
			std::cerr << "BrIt:\n";
			it.dump();
			return false;
		}
		c++;
		rp_it++;
	}
	std::cerr << "[BT] Checked " << c << " branches.\n";
	assert (rp_it == branches.end());

	return true;
}

void BranchTracker::getReplayPath(ReplayPath& rp, const iterator& it) const
{
	rp.clear();
	foreach (it2, begin(), it)
		rp.push_back(*it2);
}

static void dumpRecur(BranchTracker::SegmentRef h)
{
	if (h.isNull()) return;

	std::cerr	<< "{\nptr:" << (void*)h.get()
			<< "\noff: " << h->off()
			<< "\nlen: " << h->size() << '\n';
	if (h->size()) std::cerr << "first: " << (*h)[0].first << '\n';
	if (h->children.size() != 0) {
		std::cerr << "children: {\n";
		foreach (it, h->children.begin(), h->children.end())
			dumpRecur(*it);
	} else {
		std::cerr << "children : {}\n";
	}
	std::cerr << "}\n";
}

void BranchTracker::dump(void) const
{
	dumpRecur(getHead());
	std::cerr	<< "TOTAL SEGMENT COUNT: "
			<< Segment::getNumSegments() << '\n';
}

static const char *colors[12] =
{	"black", "red", "green", "blue",
	"yellow", "orange", "\"#0000ff\"", "\"#00ff00\"",
	"brown", "pink", "purple", "gray" };

#include <llvm/IR/Function.h>
#include "klee/Internal/Module/KInstruction.h"

static void dumpDotSeg(BranchTracker::SegmentRef head, std::ostream& os)
{
	for (unsigned i = 0; i < head->children.size(); i++)
		dumpDotSeg(head->children[i], os);

	std::string	color;
	if (head->branchSites.empty() == false) {
		color = colors[(((unsigned long)head->branchSites[0]) >> 5) % 12];
	} else
		color = "black";

	if (head->children.size() == 0) {
		os << "node [ shape = doublecircle, color=" << color << " ]; seg_";
	} else {
		os << "node [ shape = circle, color=" << color << " ]; seg_";
	}
	os << (void*)head.get() << ";";

	for (unsigned i = 0; i < head->children.size(); i++) {
		os	<< "seg_" << (void*)head.get()
			<< " -> " << "seg_" << (void*)head->children[i]
			<< " [ label = \"" << head->branches.size() << "\" ]\n";
	}
}

void BranchTracker::dumpDotFile(std::ostream& os) const
{
	SegmentRef	h(getHead());

	os << "digraph branches {\n";
	os << "size=\"8,5\"\n";
	dumpDotSeg(h, os);
	os << "\n}\n";
}

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

bool BranchTracker::push_heap_ref(HeapObject *mo) {
  if(empty())
    return false;

  tail->heapObjects.push_back(ref<HeapObject>(mo));
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
	return (*head)[0];
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

BranchTracker& BranchTracker::operator=(const BranchTracker &a)
{
	head = a.head;
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
	bool match = false;
	foreach (cit, it.curSeg->children.begin(), it.curSeg->children.end())
	{
		Segment* nextSeg = *cit;

		assert (!nextSeg->branches.empty());

		if (	(nextSeg->isBranch[0] && nextSeg->branches[0] == branch.first)
			||
			(!nextSeg->isBranch[0] &&
				nextSeg->nonBranches[0] == branch.first))
		{
		//	assert(nextSeg->branchID[0] == nextValue.second
		//	     && "Identical branch leads to different target");
			match = true;
			it.curSeg = it.tail = nextSeg;
			it.curSegIndex = 0;
			break;
		}
	}

	if (match == false) {
		++it;
		noChild = true;
	}

	return it;
}

BranchTracker::SegmentRef
BranchTracker::insert(const ReplayPath &branches)
{
	iterator it = begin();
	unsigned index = 0;
	bool noChild = false;

	for (; it != end() && index < branches.size(); index++) {
		ReplayNode	value(branches[index]);

		// special case of initial branch having more than one target;
		// the root segment in the trie is empty.
		// this is the only time we allow an empty segment.
		if (index == 0 && it.curSeg->empty()) {
			it = findChild(it, branches[index], noChild);
			if (noChild)
				break;
			continue;
		}

		// we found a divergence in the branch sequences
		if (*it != value)
			break;

		// if we're at the end of a segment,
		// then see which child (if any) has a
		// matching next value
		if (	it.curSegIndex == it.curSeg->size() - 1 &&
			index != branches.size() - 1)
		{
			it = findChild(it, branches[index+1], noChild);
			if (noChild) {
				index++;
				break;
			}
			continue;
		}

		++it;
	}

	// new set of branches is a subset of an existing path
	if (index == branches.size())
		return it.curSeg;

	if (	noChild == false &&
		((it.curSeg == head && !it.curSeg->empty()) || it.curSegIndex))
	{
		// split this segment
		splitSegment(*it.curSeg, it.curSegIndex);
		it.curSegIndex = 0;
	}

	SegmentRef oldTail = tail;
	if (noChild || it.curSeg != head) {
		SegmentRef newseg = new Segment();
		newseg->parent = noChild ? it.curSeg : it.curSeg->parent;
		newseg->parent->children.push_back(newseg.get());
		tail = it.curSeg = newseg;
	}

	for (; index < branches.size(); index++) {
		ReplayNode value(branches[index]);
		push_back(value);
	}

	tail = oldTail;
	return it.curSeg;
}

void BranchTracker::splitSegment(Segment &seg, unsigned index)
{
	Segment *newseg = new Segment();

	splitSwapData(seg, index, newseg);
	splitNonBranches(seg, index, newseg);
	splitUpdateParent(seg, newseg);
}

void BranchTracker::splitSwapData(Segment &seg, unsigned index, Segment* newseg)
{
	assert(index < seg.size());

	// newseg gets prefix, segment get suffix
	newseg->branches.insert(
		newseg->branches.begin(),
		seg.branches.begin(),
		seg.branches.begin() + index);
	newseg->isBranch.insert(
		newseg->isBranch.begin(),
		seg.isBranch.begin(),
		seg.isBranch.begin() + index);

	newseg->branchSites.insert(
		newseg->branchSites.begin(),
		seg.branchSites.begin(),
		seg.branchSites.begin()+index);

	Segment::BoolVector tempBranches(
		seg.branches.begin() + index,
		seg.branches.end());
	Segment::BoolVector tempIsBranch(
		seg.isBranch.begin() + index,
		seg.isBranch.end());

	std::swap(seg.branches, tempBranches);
	std::swap(seg.isBranch, tempIsBranch);

	Segment::BranchSites tempBranchSites(
		seg.branchSites.begin() + index,
		seg.branchSites.end());
	std::swap(seg.branchSites, tempBranchSites);

	// for safety, keep the heap object references in the parent
	std::swap(newseg->heapObjects, seg.heapObjects);
}

void BranchTracker::splitUpdateParent(Segment& seg, Segment* newseg)
{
	newseg->parent = seg.parent;
	seg.parent = newseg;

	if (newseg->parent.isNull() == false) {
		newseg->parent->children.erase(
			std::find(
				newseg->parent->children.begin(),
				newseg->parent->children.end(),
				&seg));
		newseg->parent->children.push_back(newseg);
	}

	newseg->children.push_back(&seg);

	if (head == &seg)
		head = newseg;
}

void BranchTracker::splitNonBranches(
	Segment& seg, unsigned index, Segment* newseg)
{
	Segment::NonBranchesTy nonBranchesTemp;

	for (	Segment::NonBranchesTy::iterator it = seg.nonBranches.begin(),
		ie = seg.nonBranches.end(); it != ie; )
	{
		if (it->first < index) {
			newseg->nonBranches.insert(*it);
			Segment::NonBranchesTy::iterator temp = it++;
			seg.nonBranches.erase(temp);
			continue;
		}

		nonBranchesTemp.insert(
			std::make_pair(it->first - index, it->second));
		++it;
	}

	std::swap(nonBranchesTemp, seg.nonBranches);
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

BranchTracker::iterator BranchTracker::iterator::operator++()
{
	assert(!curSeg.isNull());

	seqIndex++;

	// common case
	if (curSegIndex < curSeg->size() - 1) {
		curSegIndex++;
		return *this;
	}

	// we're at the tail, so just advance curSegIndex;
	// if 0 children, this is equivalent to end(),
	// else fork() will handle the replay logic using getSuccessor()
	if (curSeg == tail) {
		assert(curSegIndex < curSeg->size() && "BranchTracker::it OOB");
		curSegIndex++;
		return *this;
	}
	assert (curSegIndex == curSeg->size()-1);

	curSegIndex = 0;

	// we're at the end of a segment with only one child,
	// so just advance to the start of the next child
	if (curSeg->children.size() == 1) {
		curSeg = curSeg->children[0];
		return *this;
	}

	// we're at the end of a segment other than the tail,
	// backtrack to advance to segment that lets us reach the tail
	SegmentRef temp;
	for (temp = tail; temp->parent != curSeg; temp = temp->parent);
	curSeg = temp;

	return *this;
}

unsigned BranchTracker::Segment::seg_alloc_c = 0;

BranchTracker::Segment::Segment(void)
: refCount(0)
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

	/* copy tail; truncate to iterator */
	assert (it.curSeg.isNull() == false);
	tail_trunc = it.curSeg->truncatePast(it.curSegIndex);
	if (head == tail) {
		head = tail_trunc;
	}

	tail = tail_trunc;
	needNewSegment = false;
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
	std::cerr << "curSeg = " << (void*)curSeg.get()  << '\n';
	std::cerr << "curSegSize = " << curSeg->size() << '\n';
	std::cerr << "tail = " << (void*)tail.get() << '\n';
	std::cerr << "curIdx = " << curSegIndex << '\n';
}


bool BranchTracker::verifyPath(const ReplayPath& branches)
{
	ReplayPath::const_iterator	rp_it;
	unsigned			c = 0;

	rp_it = branches.begin();
	foreach (it, begin(), end()) {
		if ((*rp_it).first != (*it).first) {
			std::cerr << "MISMATCH ON NODE: " << c << '\n';
			std::cerr << "Head: " << (void*)head.get() << '\n';
			std::cerr << "BrIt:\n";
			it.dump();
			break;
		}
		c++;
		rp_it++;
	}
	assert (rp_it == branches.end());

	return true;
}

void BranchTracker::getReplayPath(ReplayPath& rp, const iterator& it) const
{
	rp.clear();
	foreach (it2, begin(), it)
		rp.push_back(*it2);
}

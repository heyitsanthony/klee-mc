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

using namespace klee;

BranchInfo BranchTracker::Segment::operator[](unsigned index) const
{
	assert(index < branches.size()); // bounds check
	const KInstruction	*ki;

	ki = branchSites[index];

	if (!isBranch[index]) {
		std::map<unsigned,unsigned>::const_iterator sit;

		sit = nonBranches.find(index);
		assert(sit != nonBranches.end());
		return BranchInfo(sit->second, ki);
	}

	return BranchInfo(branches[index], ki);
}

///

BranchTracker::BranchTracker()
: head(new Segment()), tail(head), needNewSegment(false), replayAll(false) { }

BranchTracker::BranchTracker(const BranchTracker &a)
: head(a.head), tail(a.tail), replayAll(a.replayAll)
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
{ return (head == tail && !tail->size() && head->children.empty()); }

size_t BranchTracker::size() const {
  size_t retVal = 0;
  for (SegmentRef it = tail; !it.isNull(); it = it->parent)
    retVal += it->size();
  return retVal;
}

BranchTracker::SegmentRef
BranchTracker::containing(unsigned index) const {
  unsigned prefixSize = size();
  SegmentRef it = tail;
  for (; prefixSize - tail->size() > index;
       it = it->parent)
    prefixSize -= tail->size();
  return it.get();
}

BranchInfo BranchTracker::front() const
{
	assert(!empty());
	return (*head)[0];
}

BranchInfo BranchTracker::back() const
{
	assert(!empty());
	return (*tail)[tail->size()-1];
}

BranchInfo BranchTracker::operator[](unsigned index) const
{
	unsigned	prefixSize;
	SegmentRef 	it;

	prefixSize = size();
	for (it = tail; prefixSize - tail->size() > index; it = it->parent)
		prefixSize -= tail->size(); 

	return (*it)[index - prefixSize];
}

unsigned BranchTracker::getNumSuccessors(iterator it) const
{
	if (it.isNull())
		return 0;

	assert (it.curSeg == head || !it.curSeg->empty());

	if (it == end())
		return 0;

	if (it.curIndex < it.curSeg->size())
		return 1;

	return it.curSeg->children.size();
}

BranchTracker::iterator
BranchTracker::getSuccessor(iterator it, unsigned index) const
{
	assert(it.curSeg == head || !it.curSeg->empty());
	assert (it != end() && "No successors");

	if (it.curIndex < it.curSeg->size()) {
		assert(index == 0 && "Invalid successor");
		return ++it;
	}

	assert(index < it.curSeg->children.size() && "Invalid successor");
	return iterator(
		it.curSeg->children[index], it.curSeg->children[index], 0);
}

BranchTracker& BranchTracker::operator=(const BranchTracker &a)
{
	head = a.head;
	tail = a.tail;
	replayAll = a.replayAll;
	needNewSegment = true;
	a.needNewSegment = true;
	return *this;
}

BranchTracker::iterator
BranchTracker::findChild(BranchTracker::iterator it,
                         ReplayEntry branch,
                         bool &noChild) const
{
	bool match = false;
	foreach (cit, it.curSeg->children.begin(), it.curSeg->children.end())
	{
		Segment* nextSeg = *cit;

		assert (!nextSeg->branches.empty());

		if (	(nextSeg->isBranch[0]
			&& (unsigned) nextSeg->branches[0] == branch)
			||
			(!nextSeg->isBranch[0]
			&& nextSeg->nonBranches[0] == branch))
		{
		//	assert(nextSeg->branchID[0] == nextValue.second
		//	     && "Identical branch leads to different target");
			match = true;
			it.curSeg = it.tail = nextSeg;
			it.curIndex = 0;
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
BranchTracker::insert(const ReplayPathType &branches)
{
  iterator it = begin();
  unsigned index = 0;
  bool noChild = false;

  for (; it != end() && index < branches.size(); index++) {
    BranchInfo value = BranchInfo(branches[index], 0);

    // handle special case of initial branch having more than one target; under
    // these circumstances, the root segment in the trie is empty. this is the
    // only time we allow an empty segment.
	if (!index && it.curSeg->empty()) {
		it = findChild(it, branches[index], noChild);
		if (noChild)
			break;
		else
			++it;
		continue;
	}

	// we found a divergence in the branch sequences
	if (*it != value)
		break;

	// if we're at the end of a segment, then see which child (if any) has a
	// matching next value
	if (it.curIndex == it.curSeg->size() - 1
	     && index != branches.size() - 1) {
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

  if (((it.curSeg == head && !it.curSeg->empty()) || it.curIndex) && !noChild) {
    // need to split this segment
    splitSegment(*it.curSeg, it.curIndex);
    it.curIndex = 0;
  }

  SegmentRef oldTail = tail;
  if (noChild || it.curSeg != head) {
    SegmentRef newseg = new Segment();
    newseg->parent = noChild ? it.curSeg : it.curSeg->parent;
    newseg->parent->children.push_back(newseg.get());
    tail = it.curSeg = newseg;
  }

  for (; index < branches.size(); index++) {
    BranchInfo value = BranchInfo(branches[index], 0 /* ??? */);
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


BranchInfo BranchTracker::iterator::operator*() const
{
	assert (!curSeg.isNull() &&
		!(curSeg == tail && curIndex >= curSeg->size()));
	assert (!curSeg->empty());

	if (curIndex == curSeg->size()) {
		iterator it = *this;
		++it;
		assert (!it.curSeg->empty());
		return *it;
	}

	return (*curSeg)[curIndex];
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

	// common case
	if (curIndex < curSeg->size() - 1) {
		curIndex++;
		return *this;
	}

	// we're at the end of a segment with only one child, so just advance to the
	// start of the next child
	if (curIndex == curSeg->size() - 1 && curSeg->children.size() == 1) {
		curIndex = 0;
		if (curSeg == tail)
			tail = curSeg = curSeg->children[0];
		else
			curSeg = curSeg->children[0];

		return *this;
	}

	// we're at the tail, which has either 0 or >1 children, so just advance
	// curIndex; if 0 children, this is equivalent to end(), else fork() will
	// handle the replay logic using getSuccessor()
	if (curSeg == tail) {
		assert(	curIndex < curSeg->size()
			&& "BranchTracker::iterator out of bounds");
		curIndex++;
		return *this;
	}

	// we're at the end of a segment other than the tail, so advance to the
	// segment that lets us reach the tail
	curIndex = 0;
	SegmentRef temp = tail;
	while (temp->parent != curSeg)
		temp = temp->parent;
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

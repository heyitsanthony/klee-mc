//===-- BranchTracker.cpp ---------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "BranchTracker.h"

namespace klee {

std::pair<unsigned, unsigned>
BranchTracker::Segment::operator[](unsigned index) const {
  assert(index < branches.size()); // bounds check
  unsigned id = 0;

#ifdef INCLUDE_INSTR_ID_IN_PATH_INFO
  id = branchID[index];
#endif

  if(isBranch[index])
    return std::make_pair(branches[index], id);
  else {
    std::map<unsigned,unsigned>::const_iterator sit = nonBranches.find(index);
    assert(sit != nonBranches.end()); // sanity check
    return std::make_pair(sit->second, id);
  }
}

///

BranchTracker::BranchTracker()
  : head(new Segment()), tail(head), needNewSegment(false) { }

BranchTracker::BranchTracker(const BranchTracker &a)
  : head(a.head), tail(a.tail) {

  if (a.empty()) {
    tail = head = new Segment();
    needNewSegment = false;
  }
  else {
    // fork new segments
    a.needNewSegment = true;
    needNewSegment = true;
  }
}

///

void BranchTracker::push_back(unsigned decision, unsigned id) {
  if(needNewSegment) {
    SegmentRef newSeg = new Segment();
    tail->children.push_back(newSeg.get());
    newSeg->parent = tail;
    tail = newSeg;
    needNewSegment = false;
  }
  
  if(decision == 0 || decision == 1) {
    tail->branches.push_back(decision == 0 ? false : true);
    tail->isBranch.push_back(true);
  }
  else {
    tail->branches.push_back(false); // should we keep vectors the same size?
    tail->isBranch.push_back(false);
    tail->nonBranches.insert(std::make_pair(tail->branches.size()-1,
      decision));
  }
#ifdef INCLUDE_INSTR_ID_IN_PATH_INFO
  tail->branchID.push_back(id);
#endif
}

bool BranchTracker::push_heap_ref(HeapObject *mo) {
  if(empty())
    return false;

  tail->heapObjects.push_back(ref<HeapObject>(mo));
  return true;
}

bool BranchTracker::empty() const {
  return (head == tail && !tail->size());
}

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

BranchTracker::iterator BranchTracker::skip(const BranchTracker &prefix) const {
  std::list<SegmentRef> segments, prefixSegments;
  for (SegmentRef it = tail; !it.isNull(); it = it->parent)
    segments.push_front(it);
  for (SegmentRef it = prefix.tail; !it.isNull(); it = it->parent)
    prefixSegments.push_front(it);

  std::list<SegmentRef>::const_iterator it = segments.begin(),
    prefixIt = prefixSegments.begin();
  while (prefixIt != prefixSegments.end()) {
    assert(*it == *prefixIt && "Prefixes don't match");
    it++;
    prefixIt++;
  }
  if (it == segments.end())
    return end();
  else
    return iterator(this, *it, 0);
}

std::pair<unsigned, unsigned> BranchTracker::front() const {
  assert(!empty());
  return (*head)[0];
}

std::pair<unsigned, unsigned> BranchTracker::back() const {
  assert(!empty());
  return (*tail)[tail->size()-1];
}

std::pair<unsigned, unsigned>
BranchTracker::operator[](unsigned index) const {
  unsigned prefixSize = size();
  SegmentRef it = tail;
  for (; prefixSize - tail->size() > index;
       it = it->parent)
    prefixSize -= tail->size(); 
  return (*it)[index - prefixSize];
}

BranchTracker& BranchTracker::operator=(const BranchTracker &a) {
  head = a.head;
  tail = a.tail;
  needNewSegment = true;
  a.needNewSegment = true;
  return *this;
}

void BranchTracker::truncate(iterator it) {
  assert(it.tail == tail && "truncate() called on wrong BranchTracker");
  assert(!it.curIndex && "truncate() called with non-zero branch index");
  tail = it.curSeg;
  needNewSegment = true;
}

/// iterator

std::pair<unsigned,unsigned> BranchTracker::iterator::operator*() const {
  assert(!curSeg.isNull() && !(curSeg == tail && curIndex >= curSeg->size()));
  if (curSeg->empty()) {
    iterator it = *this;
    while (it.curSeg != it.tail && it.curSeg->empty())
      ++it;
    assert(!it.curSeg->empty());
    return (*it.curSeg)[curIndex];
  }
  return (*curSeg)[curIndex];
}

// postfix operator
BranchTracker::iterator BranchTracker::iterator::operator++(int notused) {
  iterator temp = *this;
  ++(*this);
  return temp;
}

// prefix operator
BranchTracker::iterator BranchTracker::iterator::operator++() {
  assert(!curSeg.isNull());
  if (curIndex + 1 < curSeg->size())
    curIndex++;
  else if (curSeg == tail) {
    assert(curIndex <= curSeg->size()
           && "BranchTracker::iterator out of bounds");
    curIndex++;
  }
  else {
    curIndex = 0;
    SegmentRef temp = tail;
    while (temp->parent != curSeg)
      temp = temp->parent;
    curSeg = temp;
  }
  return *this;
}

}

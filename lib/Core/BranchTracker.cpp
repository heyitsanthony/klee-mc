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

BranchTracker::BranchTracker() : needNewSegment(true) { }

BranchTracker::BranchTracker(const BranchTracker &a) : segments(a.segments) {
  // fork new segments
  a.needNewSegment = true;
  needNewSegment = true;
}

///

void BranchTracker::push_back(unsigned decision, unsigned id) {
  if(needNewSegment) {
    segments.push_back(SegmentRef(new Segment()));
    needNewSegment = false;
  }
  Segment &curSeg = *(segments.back());
  
  if(decision == 0 || decision == 1) {
    curSeg.branches.push_back(decision == 0 ? false : true);
    curSeg.isBranch.push_back(true);
  }
  else {
    curSeg.branches.push_back(false); // should we keep vectors the same size?
    curSeg.isBranch.push_back(false);
    curSeg.nonBranches.insert(std::make_pair(curSeg.branches.size()-1,
      decision));
  }
#ifdef INCLUDE_INSTR_ID_IN_PATH_INFO
  curSeg.branchID.push_back(id);
#endif
}

bool BranchTracker::push_heap_ref(HeapObject *mo) {
  if(segments.empty())
    return false;

  Segment &curSeg = *(segments.back());
  curSeg.heapObjects.push_back(ref<HeapObject>(mo));
  return true;
}

bool BranchTracker::empty() const {
  return (segments.empty() ||
    (segments.size() == 1 && !(segments.front())->size()));
}

size_t BranchTracker::size() const {
  size_t retVal = 0;
  for(std::list<SegmentRef>::const_iterator sit = segments.begin();
      sit != segments.end(); ++sit)
    retVal += (*sit)->size();
  return retVal;
}

std::pair<unsigned, unsigned> BranchTracker::front() const {
  assert(!empty());
  return (*segments.front())[0];
}

std::pair<unsigned, unsigned> BranchTracker::back() const {
  assert(!empty());
  Segment &curSeg = *(segments.back());
  return curSeg[curSeg.size()-1];
}

std::pair<unsigned, unsigned>
BranchTracker::operator[](unsigned index) const {
  unsigned curIndex;

  std::list<SegmentRef>::const_iterator sit = segments.begin();
  for(curIndex = 0; curIndex + (*sit)->size() < index; sit++)
    curIndex += (*sit)->size();

  // TODO: better assert here to prevent segfault if out of bounds
  Segment &curSeg = **sit;
  assert(index - curIndex < curSeg.size());
  return curSeg[index - curIndex];
}

BranchTracker& BranchTracker::operator=(const BranchTracker &a) {
  segments = a.segments;
  needNewSegment = true;
  a.needNewSegment = true;
  return *this;
}

/// iterator

std::pair<unsigned,unsigned> BranchTracker::iterator::operator*() const {
  assert(curSeg != BranchTracker::nullList.end() && curIndex < (*curSeg)->size());
  return (*(*curSeg))[curIndex];
}

// postfix operator
BranchTracker::iterator BranchTracker::iterator::operator++(int notused) {
  iterator temp = *this;
  ++(*this);
  return temp;
}

// prefix operator
BranchTracker::iterator& BranchTracker::iterator::operator++() {
  assert(curSeg != BranchTracker::nullList.end());
  if(curIndex+1 < (*curSeg)->size())
    curIndex++;
  else {
    curIndex = 0;
    curSeg++;
    if(curSeg == tracker->segments.end())
      curSeg = BranchTracker::nullList.end();
  }
  return *this;
}

const std::list<BranchTracker::SegmentRef> BranchTracker::nullList;

}

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
  : head(new Segment()), tail(head), needNewSegment(false),
    replayAll(false) { }

BranchTracker::BranchTracker(const BranchTracker &a)
  : head(a.head), tail(a.tail), replayAll(a.replayAll) {

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
    tail->branches.push_back(decision == 1);
    tail->isBranch.push_back(true);
  }
  else {
    tail->branches.push_back(false);
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
  return (head == tail && !tail->size() && head->children.empty());
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
  if (tail == prefix.tail)
    return end();

  SegmentRef temp = tail;
  for (; !temp.isNull(); temp = temp->parent) {
    if (!temp->parent.isNull() && temp->parent == prefix.tail)
      break;
  }

  assert(!temp.isNull() && "Prefixes don't match");
  return iterator(this, temp, 0);
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

unsigned BranchTracker::getNumSuccessors(iterator it) const {
  if (it.isNull())
    return 0;

  assert(it.curSeg == head || !it.curSeg->empty());
  if (it == end())
    return 0;
  else if (it.curIndex < it.curSeg->size())
    return 1;
  else
    return it.curSeg->children.size();
}

BranchTracker::iterator
BranchTracker::getSuccessor(iterator it, unsigned index) const {
  assert(it.curSeg == head || !it.curSeg->empty());
  if (it == end())
    assert(0 && "No successors");
  else if (it.curIndex < it.curSeg->size()) {
    assert(index == 0 && "Invalid successor");
    return ++it;
  }
  else {
    assert(index < it.curSeg->children.size() && "Invalid successor");
    return iterator(it.curSeg->children[index], it.curSeg->children[index], 0);
  }  
}

///

BranchTracker& BranchTracker::operator=(const BranchTracker &a) {
  head = a.head;
  tail = a.tail;
  replayAll = a.replayAll;
  needNewSegment = true;
  a.needNewSegment = true;
  return *this;
}

void BranchTracker::truncate(iterator it) {
  assert(it.tail == tail && "truncate() called on wrong BranchTracker");
  assert(!it.curIndex && "truncate() called with non-zero branch index");
  assert(!it.curSeg->parent.isNull() && "truncate() called on root segment");
  tail = it.curSeg->parent;
  needNewSegment = true;
}

void BranchTracker::advance(iterator it) {
  assert(!it.curSeg.isNull());
  tail = it.curSeg;
}

BranchTracker::SegmentRef
BranchTracker::insert(const ReplayPathType &branches) {
  iterator it = begin();
  unsigned index = 0;
  bool noChild = false;
  for (; it != end() && index < branches.size(); index++) {
#ifdef INCLUDE_INSTR_ID_IN_PATH_INFO
    std::pair<unsigned,unsigned> value = branches[index];
#else
    std::pair<unsigned,unsigned> value = std::make_pair(branches[index], 0);
#endif
    if (*it != value)
      break;
    // if we're at the end of a segment, then see which child (if any) has a
    // matching next value
    if (it.curIndex == it.curSeg->size() - 1 && index != branches.size() - 1) {
      bool match = false;
      for (Segment::SegmentVector::iterator cit = it.curSeg->children.begin(),
           cie = it.curSeg->children.end(); cit != cie; ++cit) {
        Segment* nextSeg = *cit;
        assert(!nextSeg->branches.empty());
#ifdef INCLUDE_INSTR_ID_IN_PATH_INFO
      std::pair<unsigned,unsigned> nextValue = branches[index+1];
#else
      std::pair<unsigned,unsigned> nextValue =
        std::make_pair(branches[index+1], 0);
#endif
        if ((nextSeg->isBranch[0]
             && (unsigned) nextSeg->branches[0] == nextValue.first)
            || (!nextSeg->isBranch[0]
                && nextSeg->nonBranches[0] == nextValue.first)) {
#ifdef INCLUDE_INSTR_ID_IN_PATH_INFO
          assert(nextSeg->branchID[0] == nextValue.second
                 && "Identical branch leads to different target");
#endif
          match = true;
          it.curSeg = it.tail = nextSeg;
          it.curIndex = 0;
          break;
        }
      }
      if (!match) {
        ++it;
        noChild = true;
        index++;
        break;
      }
    }
    else
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
    SegmentRef newSeg = new Segment();
    newSeg->parent = noChild ? it.curSeg : it.curSeg->parent;
    newSeg->parent->children.push_back(newSeg.get());
    tail = it.curSeg = newSeg;
  }

  for (; index < branches.size(); index++) {
#ifdef INCLUDE_INSTR_ID_IN_PATH_INFO
    std::pair<unsigned,unsigned> value = branches[index];
#else
    std::pair<unsigned,unsigned> value = std::make_pair(branches[index], 0);
#endif
    push_back(value);
  }
  tail = oldTail;
  return it.curSeg;
}

BranchTracker::iterator BranchTracker::split(iterator it) {
  if (it.curIndex < it.curSeg->size()) {
    splitSegment(*it.curSeg, it.curIndex);
    it.curSeg = it.curSeg->parent;
  }
  return it;
}

void BranchTracker::splitSegment(Segment &segment, unsigned index) {
  assert(index < segment.size());
  Segment *newSeg = new Segment();
  // newSeg gets prefix, segment get suffix
  newSeg->branches.insert(newSeg->branches.begin(), segment.branches.begin(),
                          segment.branches.begin() + index);
  newSeg->isBranch.insert(newSeg->isBranch.begin(), segment.isBranch.begin(),
                          segment.isBranch.begin() + index);
#ifdef INCLUDE_INSTR_ID_IN_PATH_INFO
  newSeg->branchID.insert(newSeg->branchID.begin(),
                          segment.branchID.begin(),
                          segment.branchID.begin()+index);
#endif

  Segment::BoolVector tempBranches(segment.branches.begin() + index,
                                   segment.branches.end());
  Segment::BoolVector tempIsBranch(segment.isBranch.begin() + index,
                                   segment.isBranch.end());

  std::swap(segment.branches, tempBranches);
  std::swap(segment.isBranch, tempIsBranch);

#ifdef INCLUDE_INSTR_ID_IN_PATH_INFO
  Segment::BranchIDs tempBranchIDs(segment.branchID.begin() + index,
                                   segment.branchID.end());
  std::swap(segment.branchID, tempBranchIDs);
#endif
  // for safety, keep the heap object references in the parent
  std::swap(newSeg->heapObjects,segment.heapObjects);

  Segment::NonBranchesTy nonBranchesTemp;
  for (Segment::NonBranchesTy::iterator it = segment.nonBranches.begin(),
       ie = segment.nonBranches.end(); it != ie; ) {
    if (it->first < index) {
      newSeg->nonBranches.insert(*it);
      Segment::NonBranchesTy::iterator temp = it++;
      segment.nonBranches.erase(temp);
    }
    else {
      nonBranchesTemp.insert(std::make_pair(it->first - index, it->second));
      ++it;
    }
  }

  std::swap(nonBranchesTemp, segment.nonBranches);

  newSeg->parent = segment.parent;
  segment.parent = newSeg;
  if (!newSeg->parent.isNull()) {
    newSeg->parent->children.erase(std::find(newSeg->parent->children.begin(),
                                             newSeg->parent->children.end(),
                                             &segment));
    newSeg->parent->children.push_back(newSeg);
  }
  newSeg->children.push_back(&segment);

  if (head == &segment)
    head = newSeg;
}

/// iterator

std::pair<unsigned,unsigned> BranchTracker::iterator::operator*() const {
  assert(!curSeg.isNull() && !(curSeg == tail && curIndex >= curSeg->size()));
  assert(!curSeg->empty());
  if (curIndex == curSeg->size()) {
    iterator it = *this;
    ++it;
    assert(!it.curSeg->empty());
    return *it;
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
  // common case
  if (curIndex < curSeg->size() - 1)
    curIndex++;
  // we're at the end of a segment with only one child, so just advance to the
  // start of the next child
  else if (curIndex == curSeg->size() - 1 && curSeg->children.size() == 1) {
    curIndex = 0;
    if (curSeg == tail)
      tail = curSeg = curSeg->children[0];
    else
      curSeg = curSeg->children[0];
  }
  // we're at the tail, which has either 0 or >1 children, so just advance
  // curIndex; if 0 children, this is equivalent to end(), else fork() will
  // handle the replay logic using getSuccessor()
  else if (curSeg == tail) {
    assert(curIndex < curSeg->size()
           && "BranchTracker::iterator out of bounds");
    curIndex++;
  }
  // we're at the end of a segment other than the tail, so advance to the
  // segment that lets us reach the tail
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

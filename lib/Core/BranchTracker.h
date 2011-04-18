//===-- BranchTracker.h -----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_BRANCHTRACKER_H
#define KLEE_BRANCHTRACKER_H

#include "Memory.h"
#include "MemoryManager.h"

#include <map>
#include <vector>
#include <list>
#include <cassert>
#include <climits>

#include <iostream>

namespace klee {

  struct BranchDecision
  {
    enum { ID_bits = sizeof(unsigned) * CHAR_BIT };
    unsigned ID;
    unsigned targetIndex;
    friend bool operator<(const BranchDecision& a, const BranchDecision& b)
    {
      return a.ID < b.ID || (!(b.ID < a.ID) && a.targetIndex < b.targetIndex);
    }
    BranchDecision() { }
    BranchDecision(unsigned _targetIndex, unsigned _ID)
      : ID(_ID), targetIndex(_targetIndex) { }
    BranchDecision(std::pair<unsigned, unsigned> a)
      : ID(a.second), targetIndex(a.first) { }
  };

  /// Implements a trie data structure for tracking branch decisions
  class BranchTracker {

  private:
    /// A segment is a decision sequence that forms an edge in the path tree
    struct Segment {
      Segment() : refCount(0) { }
      ~Segment() { }

      // vectors may not be the ideal data struct here, but STL provides a
      // partial specialization for vector<bool> for optimized bit vector
      // storage
      std::vector<bool> branches; // 0,1 branch decision (compressed for bools)
      std::vector<bool> isBranch; // 1 = branch, 0 = switch (nonBranches)
      std::map<unsigned,unsigned> nonBranches; // key = index, value = target
#ifdef INCLUDE_INSTR_ID_IN_PATH_INFO
      std::vector<unsigned> branchID;
#endif
      std::list<ref<HeapObject> > heapObjects;
      unsigned refCount;

      inline size_t size() const { return branches.size(); }
      std::pair<unsigned, unsigned> operator[](unsigned index) const;
    }; // class Segment

    class SegmentRef {
    public:
      // default constructor: null reference
      SegmentRef() : content(0) { }
      // normal constructor: increment refcount
      SegmentRef(Segment *a) : content(a) {
        content->refCount++;
      }
      // copy constructor: increment refcount
      SegmentRef(const SegmentRef &a) : content(a.content) {
        content->refCount++;
      }
      ~SegmentRef() {
        if(content) {
          assert(content->refCount);
          content->refCount--;
          if(!content->refCount)
            delete content;
        }
      } // ~SegmentRef()

    private:
      Segment *content;

    public:
      inline Segment& operator*() const { return *content; }
      inline Segment* operator->() const { return &(*content); }
      inline bool operator==(const SegmentRef &a) const {
        return (content == a.content);
      }

    }; // class SegmentRef

  public:

    class iterator {
    public:
      iterator() : tracker(0), curSeg(BranchTracker::nullList.end()), curIndex(0) { }
      iterator(const BranchTracker *_tracker)
        : tracker(_tracker), curSeg(_tracker->segments.begin()), curIndex(0) { }
      iterator(const BranchTracker *_tracker,
               std::list<SegmentRef>::const_iterator _curSeg,
               unsigned _curIndex = 0)
        : tracker(_tracker), curSeg(_curSeg), curIndex(_curIndex) { }
    
    private:
      friend class BranchTracker;
      const BranchTracker *tracker;
      std::list<SegmentRef>::const_iterator curSeg;
      unsigned curIndex;

    public:
      std::pair<unsigned, unsigned> operator*() const;
      iterator operator++(int notused);
      iterator& operator++();
      inline bool operator==(iterator a) const {
        return (curIndex == a.curIndex && *curSeg == *a.curSeg);
      }
      inline bool operator!=(iterator a) const { return !(*this == a); }
    }; // class iterator

  public:
    BranchTracker();
    BranchTracker(const BranchTracker &a);
    ~BranchTracker() { }

  private:
    friend class iterator;
    std::list<SegmentRef> segments;
    mutable bool needNewSegment;
    static const std::list<SegmentRef> nullList; // for EOL sentinel

  public:
    bool empty() const;
    size_t size() const;
    iterator begin() const { return iterator(this); }
    iterator end() const {
      return iterator(this, BranchTracker::nullList.end());
    }
    // these return (decision,id) pairs; id is undefined ifndef
    // INCLUDE_INSTR_ID_IN_PATH_INFO
    std::pair<unsigned, unsigned> front() const;
    std::pair<unsigned, unsigned> back() const;
    std::pair<unsigned, unsigned> operator[](unsigned index) const;
    BranchTracker& operator=(const BranchTracker &a);

    void push_back(unsigned decision, unsigned id = 0);
    void push_back(const std::pair<unsigned,unsigned> &a) {
      push_back(a.first, a.second);
    }
    
    bool push_heap_ref(HeapObject *a);
  }; // class BranchTracker


}

#endif

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
  public:
    class Segment;
    typedef ref<Segment> SegmentRef;

    /// A segment is a decision sequence that forms an edge in the path tree
    struct Segment {
      Segment() : refCount(0) { }
      ~Segment() {
        if (!parent.isNull()) {
          std::vector<Segment*>::iterator it =
            std::find(parent->children.begin(), parent->children.end(), this);
          assert(it != parent->children.end());
          parent->children.erase(it);
        }
      }

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
      std::vector<Segment*> children; // not ref to avoid circular references
      SegmentRef parent;

      inline size_t size() const { return branches.size(); }
      inline bool empty() const { return branches.empty(); }
      std::pair<unsigned, unsigned> operator[](unsigned index) const;
      int compare(Segment &a) const {
        if (this < &a)
          return -1;
        else if (this == &a)
          return 0;
        else
          return 1;
      }
    }; // class Segment

    class iterator {
      friend class BranchTracker;
    public:
      iterator() : curIndex(0) { }
      iterator(const BranchTracker *tracker)
        : curSeg(tracker->head), tail(tracker->tail), curIndex(0) { }
      iterator(const BranchTracker *tracker,
               SegmentRef _curSeg,
               unsigned _curIndex = 0)
        : curSeg(_curSeg), tail(tracker->tail), curIndex(_curIndex) { }
      ~iterator() { }

    private:
      SegmentRef curSeg, tail;
      unsigned curIndex;
    public:
      std::pair<unsigned, unsigned> operator*() const;
      iterator operator++(int notused);
      iterator operator++();
      inline bool operator==(iterator a) const {
        return (curSeg.get() == a.curSeg.get() && curIndex == a.curIndex);
      }
      inline bool operator!=(iterator a) const { return !(*this == a); }
    }; // class iterator

  public:
    BranchTracker();
    BranchTracker(const BranchTracker &a);
    ~BranchTracker() { }

  private:
    friend class iterator;
    SegmentRef head, tail;
    mutable bool needNewSegment;

  public:
    bool empty() const;
    size_t size() const;
    iterator begin() const {
      if (empty())
        return end();
      else
        return iterator(this);
    }
    iterator end() const {
      return iterator(this, tail, tail->size());
    }
    /// generates an iterator at the first branch not contained in 'prefix',
    /// or end() if the two BranchTrackers are identical
    iterator skip(const BranchTracker &prefix) const;

    iterator translate(iterator it) const {
      return iterator(this, it.curSeg, it.curIndex);
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

    /// truncates the BranchTracker at the position of the current iterator
    void truncate(iterator it);

  private:
    SegmentRef containing(unsigned index) const;
  };
}

#endif

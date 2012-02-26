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

#include "klee/Interpreter.h"

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
    class Segment {
    private:
    public:
      Segment() : refCount(0) { }
      ~Segment() {
        if (!parent.isNull()) {
          SegmentVector::iterator it =
            std::find(parent->children.begin(), parent->children.end(), this);
          assert(it != parent->children.end());
          parent->children.erase(it);
        }
      }

      // vectors may not be the ideal data struct here, but STL provides a
      // partial specialization for vector<bool> for optimized bit vector
      // storage
      typedef std::vector<bool> BoolVector;
      BoolVector branches; // 0,1 branch decision (compressed for bools)
      BoolVector isBranch; // 1 = branch, 0 = switch (nonBranches)
      typedef std::map<unsigned, unsigned, std::less<unsigned> > NonBranchesTy;
      NonBranchesTy nonBranches; // key = index, value = target
#ifdef INCLUDE_INSTR_ID_IN_PATH_INFO
      typedef std::vector<unsigned> BranchIDs;
      BranchIDs branchID;
#endif
      typedef std::list<ref<HeapObject> > HeapObjectsTy;
      HeapObjectsTy heapObjects;
      unsigned refCount;
      typedef std::vector<Segment*> SegmentVector;
      SegmentVector children; // not ref to avoid circular references
      SegmentRef parent;

    public:
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
      iterator(const BranchTracker *tracker, SegmentRef _curSeg,
               unsigned _curIndex = 0)
        : curSeg(_curSeg), tail(tracker->tail), curIndex(_curIndex) { }

      ~iterator() { }

    private:
      iterator(SegmentRef _tail, SegmentRef _curSeg, unsigned _curIndex)
        : curSeg(_curSeg), tail(_tail), curIndex(_curIndex) { }

    private:
      SegmentRef curSeg, tail;
      unsigned curIndex;
    public:
      bool isNull() const { return curSeg.isNull(); }
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
    bool replayAll;

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
      Segment *temp = tail.get();
      while (replayAll && !temp->children.empty())
        temp = temp->children.front();
      return iterator(this, temp, temp->size());
    }
    /// generates an iterator at the first branch not contained in 'prefix',
    /// or end() if the two BranchTrackers are identical
    iterator skip(const BranchTracker &prefix) const;

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

    /// sets tail to iterator's current segment
    void advance(iterator it);

    /// split the current segment if the iterator is not at the end of that
    /// segment
    iterator split(iterator it);

    /// insert a new series of branches; returns a reference to the new tail
    /// NOTE: this reference should be stored somewhere or the new branches will
    /// immediately be garbage collected.
    SegmentRef insert(const ReplayPathType &branches);

    unsigned getNumSuccessors(iterator it) const;
    iterator getSuccessor(iterator it, unsigned index) const;

    void setReplayAll(bool _replayAll) { replayAll = _replayAll; }
  private:
    void splitSegment(Segment &segment, unsigned index);
    SegmentRef containing(unsigned index) const;
    typedef unsigned ReplayEntry;
    iterator findChild(iterator it, ReplayEntry branch, bool &noChild) const;
  };
}

#endif

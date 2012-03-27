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

namespace klee
{
class KInstruction;

/// Implements a trie data structure for tracking branch decisions
//
typedef std::pair<unsigned, const KInstruction*> BranchInfo;
class BranchTracker
{
public:
	class Segment;
	typedef ref<Segment> SegmentRef;

/// A segment is a decision sequence that forms an edge in the path tree
class Segment
{
public:
	Segment(void);
	~Segment(void);

	// vectors may not be the ideal data struct here, but STL provides a
	// partial specialization for vector<bool> for optimized bit vector
	// storage
	typedef std::vector<bool> BoolVector;
	typedef std::map<unsigned, unsigned, std::less<unsigned> > NonBranchesTy;
	typedef std::vector<const KInstruction*> BranchSites;
	typedef std::list<ref<HeapObject> > HeapObjectsTy;
	typedef std::vector<Segment*> SegmentVector;

	BoolVector branches; // 0,1 branch decision (compressed for bools)
	BoolVector isBranch; // 1 = branch, 0 = switch (nonBranches)

	NonBranchesTy nonBranches; // key = index, value = target

	BranchSites	branchSites;

	HeapObjectsTy	heapObjects;
	unsigned	refCount;
	SegmentVector	children; // not ref to avoid circular references
	SegmentRef	parent;

	inline size_t size() const { return branches.size(); }
	inline bool empty() const { return branches.empty(); }
	BranchInfo operator[](unsigned index) const;
	int compare(Segment &a) const
	{
		if (this < &a)
			return -1;
		if (this == &a)
			return 0;
		return 1;
	}
private:
	static unsigned seg_alloc_c;
};

class iterator
{
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
	SegmentRef	curSeg, tail;
	unsigned	curIndex;

	iterator(SegmentRef _tail, SegmentRef _curSeg, unsigned _curIndex)
	: curSeg(_curSeg), tail(_tail), curIndex(_curIndex)
	{ }
public:
	bool isNull() const { return curSeg.isNull(); }
	BranchInfo operator*() const;
	iterator operator++(int notused);
	iterator operator++();
	inline bool operator==(iterator a) const
	{ return (curSeg.get() == a.curSeg.get() && curIndex == a.curIndex); }
	inline bool operator!=(iterator a) const { return !(*this == a); }
};

public:
	BranchTracker();
	BranchTracker(const BranchTracker &a);
	~BranchTracker() { }

private:
	friend class iterator;
	SegmentRef	head, tail;
	mutable bool	needNewSegment;
	bool		replayAll;

public:
	bool empty() const;
	size_t size() const;
	iterator begin() const { return (empty()) ? end() : iterator(this); }
	iterator end() const
	{
		Segment *temp = tail.get();
		while (replayAll && !temp->children.empty())
			temp = temp->children.front();
		return iterator(this, temp, temp->size());
	}


	BranchInfo front() const;
	BranchInfo back() const;
	BranchInfo operator[](unsigned index) const;
	BranchTracker& operator=(const BranchTracker &a);

	void push_back(unsigned decision, const KInstruction* ki = 0);
	void push_back(const BranchInfo &a) { push_back(a.first, a.second); }

	bool push_heap_ref(HeapObject *a);

	/// insert a new series of branches; returns a reference to the new tail
	/// NOTE: this reference should be stored somewhere
	/// or the new branches will immediately be garbage collected.
	SegmentRef insert(const ReplayPathType &branches);

	unsigned getNumSuccessors(iterator it) const;
	iterator getSuccessor(iterator it, unsigned index) const;

	void setReplayAll(bool _replayAll) { replayAll = _replayAll; }
private:
	void splitSegment(Segment &segment, unsigned index);
	void splitNonBranches(Segment& seg, unsigned index, Segment* newSeg);
	void splitUpdateParent(Segment& seg, Segment* newSeg);
	void splitSwapData(Segment &seg, unsigned index, Segment* newSeg);

	SegmentRef containing(unsigned index) const;
	typedef unsigned ReplayEntry;
	iterator findChild(iterator it, ReplayEntry branch, bool &noChild) const;
};
}

#endif

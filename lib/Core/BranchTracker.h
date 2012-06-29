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

#include "klee/util/Ref.h"
#include "klee/Interpreter.h"

#include <map>
#include <vector>
#include <list>
#include <cassert>
#include <climits>

namespace klee
{
class KInstruction;
class HeapObject;

/// Implements a trie data structure for tracking branch decisions
typedef std::pair<unsigned, const KInstruction*> BranchInfo;
class BranchTracker
{
friend class iterator;
public:
	class Segment;
	typedef ref<Segment> SegmentRef;
	#include "BTSegment.h"
	#include "BTIter.h"

	BranchTracker();
	BranchTracker(const BranchTracker &a);
	~BranchTracker() { }

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

	SegmentRef	head, tail;
	mutable bool	needNewSegment;
	bool		replayAll;
};
}

#endif

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
	iterator begin() const;
	iterator end() const;

	ReplayNode front() const;
	ReplayNode back() const;
	ReplayNode operator[](unsigned index) const;
	BranchTracker& operator=(const BranchTracker &a);

	void push_back(unsigned decision, const KInstruction* ki = 0);
	void push_back(const ReplayNode &a) { push_back(a.first, a.second); }

	bool push_heap_ref(HeapObject *a);

	/// insert a new series of branches; returns a reference to the new tail
	/// NOTE: this reference should be stored somewhere
	/// or the new branches will immediately be garbage collected.
	SegmentRef insert(const ReplayPath &branches);

	void truncatePast(iterator& it);

	bool verifyPath(const ReplayPath& branches);

	void getReplayPath(ReplayPath& rp, const iterator& it) const;
	void getReplayPath(ReplayPath& rp) const { getReplayPath(rp, end()); }

	SegmentRef getHead(void) const;
	SegmentRef getTail(void) const { return tail; }
	void dump(void) const;

	void dumpDotFile(std::ostream& os) const;
	void dumpPathsFile(std::ostream& os) const;
private:
	void splitSegment(Segment &segment, unsigned index);

	SegmentRef containing(unsigned index) const;
	iterator findChild(iterator it, ReplayNode branch, bool &noChild) const;

	SegmentRef	head, tail;
	mutable bool	needNewSegment;

	static BranchTracker	dummyHead;
};
}

#endif

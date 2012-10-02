#ifndef BTITER_H
#define BTITER_H

#ifndef KLEE_BRANCHTRACKER_H
#error include branchtracker.h instead
#endif

class iterator
{
	friend class BranchTracker;
public:
	iterator() : curSegIndex(0) { }
	
	iterator(const BranchTracker *tracker)
	: curSeg(tracker->getHead())
	, tail(tracker->tail)
	, curSegIndex(0)
	, seqIndex(0) {}

	iterator(
		const BranchTracker *tracker,
		SegmentRef _curSeg,
		unsigned _curSegIndex = 0)
	: curSeg(_curSeg)
	, tail(tracker->tail)
	, curSegIndex(_curSegIndex)
	, seqIndex(curSeg->off() + curSegIndex) {}

	~iterator() { }
private:
	SegmentRef	curSeg, tail;
	unsigned	curSegIndex, seqIndex;

	bool mayDeref(void) const;
	void bump(void);
public:
	bool isNull() const { return curSeg.isNull(); }
	void reseat(void);
	ReplayNode operator*() const;
	iterator operator++(int notused);
	iterator operator++();
	inline bool operator==(const iterator& a) const
	{
		if (!mayDeref()) return !a.mayDeref();
		return (curSeg.get() == a.curSeg.get()
			&& curSegIndex == a.curSegIndex
			&& tail.get() == a.tail.get());
	}
	inline bool operator!=(const iterator& a) const { return !(*this==a); }
	void dump(void) const;
	void assumeTail(const iterator& it) { tail = it.tail; }
	unsigned getSeqIdx(void) const { return seqIndex; }
};


#endif

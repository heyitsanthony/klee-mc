#ifndef BTITER_H
#define BTITER_H

#ifndef KLEE_BRANCHTRACKER_H
#error include branchtracker.h instead
#endif

class iterator
{
	friend class BranchTracker;
public:
	iterator() : curIndex(0) { }
	
	iterator(const BranchTracker *tracker)
	: curSeg(tracker->head)
	, tail(tracker->tail)
	, curIndex(0) { }

	iterator(
		const BranchTracker *tracker,
		SegmentRef _curSeg,
		unsigned _curIndex = 0)
	: curSeg(_curSeg)
	, tail(tracker->tail)
	, curIndex(_curIndex) { }

	~iterator() { }
private:
	SegmentRef	curSeg, tail;
	unsigned	curIndex;

	iterator(SegmentRef _tail, SegmentRef _curSeg, unsigned _curIndex)
	: curSeg(_curSeg)
	, tail(_tail)
	, curIndex(_curIndex)
	{ }

	bool mayDeref(void) const;
public:
	bool isNull() const { return curSeg.isNull(); }
	ReplayNode operator*() const;
	iterator operator++(int notused);
	iterator operator++();
	inline bool operator==(const iterator& a) const
	{	if (!mayDeref()) return !a.mayDeref();
		return (curSeg.get() == a.curSeg.get()
			&& curIndex == a.curIndex
			&& tail.get() == a.tail.get());
	}
	inline bool operator!=(const iterator& a) const { return !(*this==a); }
	void dump(void) const;
	void assumeTail(const iterator& it) { tail = it.tail; }
};


#endif

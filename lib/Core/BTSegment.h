#ifndef BTSEGMENT_H
#define BTSEGMENT_H

#ifndef KLEE_BRANCHTRACKER_H
#error include branchtracker.h instead
#endif

#define BT_SEG_CANARY	0x87654321

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

	BoolVector	branches; // 0,1 branch decision (compressed for bools)
	BoolVector	isBranch; // 1 = branch, 0 = switch (nonBranches)

	NonBranchesTy	nonBranches; // key = index, value = target

	BranchSites	branchSites;

	HeapObjectsTy	heapObjects;
	unsigned	refCount;
	SegmentVector	children; // not ref to avoid circular references
	SegmentRef	parent;

	inline size_t size() const { return branches.size(); }
	inline bool empty() const { return branches.empty(); }
	ReplayNode operator[](unsigned index) const;
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

#endif

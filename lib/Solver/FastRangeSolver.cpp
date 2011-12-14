#include "klee/Constraints.h"
#include "klee/Expr.h"
#include "klee/Internal/System/Time.h"
#include "static/Sugar.h"
#include "klee/Solver.h"
#include "klee/util/Assignment.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprUtil.h"

#include "llvm/ADT/APInt.h"
#include "llvm/Support/raw_ostream.h"

#include "IncompleteSolver.h"
#include "StagedSolver.h"

#include <set>
#include <stack>
#include <queue>
#include <deque>
#include <iostream>

//#define FRS_DEBUG

using namespace klee;
using namespace llvm;

/***/

#define FastRangeTimeout 5.

// Hacker's Delight, pgs 58-63
static uint64_t minOR(uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
	uint64_t temp, m = ((uint64_t) 1)<<63;
	while (m) {
		if (~a & c & m) {
			temp = (a | m) & -m;
			if (temp <= b) { a = temp; break; }
		} else if (a & ~c & m) {
			temp = (c | m) & -m;
			if (temp <= d) { c = temp; break; }
		}
		m >>= 1;
	}

	return a | c;
}

static uint64_t maxOR(uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
	uint64_t temp, m = ((uint64_t) 1)<<63;

	while (m) {
		if (b & d & m) {
			temp = (b - m) | (m - 1);
			if (temp >= a) { b = temp; break; }
			temp = (d - m) | (m -1);
			if (temp >= c) { d = temp; break; }
		}
		m >>= 1;
	}

	return b | d;
}

static uint64_t minAND(uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
	uint64_t temp, m = ((uint64_t) 1)<<63;
	while (m) {
		if (~a & ~c & m) {
			temp = (a | m) & -m;
			if (temp <= b) { a = temp; break; }
			temp = (c | m) & -m;
			if (temp <= d) { c = temp; break; }
		}
		m >>= 1;
	}

	return a & c;
}
static uint64_t maxAND(uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
	uint64_t temp, m = ((uint64_t) 1)<<63;
	while (m) {
		if (b & ~d & m) {
			temp = (b & ~m) | (m - 1);
			if (temp >= a) { b = temp; break; }
		} else if (~b & d & m) {
			temp = (d & ~m) | (m - 1);
			if (temp >= c) { d = temp; break; }
		}
		m >>= 1;
	}

	return b & d;
}

/***/

class FastRangeSolver : public SolverImpl
{
public:
	FastRangeSolver();
	~FastRangeSolver();

	virtual bool computeSat(const Query& query);
	virtual ref<Expr> computeValue(const Query&);
	virtual Solver::Validity computeValidity(const Query& query) {
		failQuery();
		return Solver::Unknown;
	}
	virtual bool computeInitialValues(const Query&, Assignment&);
	virtual void printName(int level = 0) const
	{
		klee_message("%*s" "FastRangeSolver", 2*level, "");
	}

private:
	unsigned numQueries;
	unsigned numSuccess;
	unsigned numUnsupported;
};


FastRangeSolver::FastRangeSolver()
: numQueries(0)
, numSuccess(0)
, numUnsupported(0) { }

FastRangeSolver::~FastRangeSolver()
{
	klee_warning(
		"FastRangeSolver Statistics:\n%u queries, "
		"%u solved (%f%%), %u unsupported\n",
		 numQueries, numSuccess,
		 (float) numSuccess / (float) numQueries * 100.f,
		 numUnsupported);
}

class ValueSet
{
private:
	APInt m_min, m_max;

public:
	ValueSet() : m_min(1, true), m_max(1, false) { }
	ValueSet(unsigned bits) : m_min(bits, 1), m_max(bits, 0) { }
	ValueSet(unsigned bits, uint64_t val) : m_min(bits, val), m_max(bits, val) { }
	ValueSet(APInt _min, APInt _max) : m_min(_min), m_max(_max) { }
	ValueSet(uint64_t _min, APInt _max)
		: m_min(_max.getBitWidth(), _min), m_max(_max) { }
	ValueSet(APInt _min, uint64_t _max)
		: m_min(_min), m_max(_min.getBitWidth(), _max) { }
	ValueSet(ValueSet _min, ValueSet _max)
		: m_min(_min.m_min), m_max(_max.m_min) {
		assert(_min.unique() && _max.unique());
	}

	bool empty() const { return m_min.ugt(m_max); }
	bool unique() const { return m_min == m_max; }

	uint64_t value() const {
		assert(unique() && "Value not unique");
		return m_min.getZExtValue();
	}

	unsigned getWidth() const {
		assert(m_min.getBitWidth() == m_max.getBitWidth());
		return m_min.getBitWidth();
	}

	// returns the size of the range - 1; range must not be empty
	// (actual size would overflow in the case of 64-bit full ranges)
	uint64_t size() const {
		assert(!empty());
		return (m_max - m_min).getZExtValue();
	}

	// returns the relative size in [0,1] of a range (relative to the max)
	double inline rel_size() const {
		assert(!empty());
		APFloat fsize(0.0);
		APFloat::opStatus s;

		s = fsize.convertFromAPInt(
			m_max - m_min, false, APFloat::rmNearestTiesToEven);
		assert(s == APFloat::opOK || s == APFloat::opInexact);

		APFloat maxsize(0.0);
		s = maxsize.convertFromAPInt(
			APInt::getMaxValue(getWidth()),
			false,
			APFloat::rmNearestTiesToEven);

		assert(s == APFloat::opOK || s == APFloat::opInexact);
		s = fsize.divide(maxsize, APFloat::rmNearestTiesToEven);
		assert(s == APFloat::opOK || s == APFloat::opInexact);
		return fsize.convertToDouble();
	}

	bool contains(uint64_t val) const {
		APInt _val(getWidth(), val);
		return m_min.ule(_val) && m_max.uge(_val);
	}

	bool superset(ValueSet &b) const {
		return m_min.ule(b.m_min) && m_max.uge(b.m_max);
	}
	bool intersects(ValueSet &b) const {
		return m_min.ule(b.m_max) && m_max.uge(b.m_min);
	}
	bool isFullRange() const {
		return m_min == 0 && m_max == APInt::getMaxValue(getWidth());
	}
	bool isOnlyNegative() const {
		return m_min.isNegative() && m_max.isNegative();
	}
	bool isOnlyNonNegative() const {
		return !m_min.isNegative() && !m_max.isNegative();
	}

	uint64_t min() const { return m_min.getZExtValue(); }
	uint64_t max() const { return m_max.getZExtValue(); }

	// monotonically decrease value set
	bool monoSet(ValueSet b) {
		bool hasChanged = false;
		if (	m_min.ugt(b.m_max.zextOrTrunc(m_min.getBitWidth())) ||
			m_max.ult(b.m_min.zextOrTrunc(m_min.getBitWidth())))
		{	// disjoint sets
			hasChanged = true;
			m_min = 1; // set to empty
			m_max = 0;
		} else {
			if (m_min.ult(b.m_min)) {
				hasChanged = true;
				m_min = b.m_min;
			}
			if (m_max.ugt(b.m_max)) {
				hasChanged = true;
				m_max = b.m_max;
			}
		}
		return hasChanged;
	}

	bool unionSet(ValueSet b) {
		bool hasChanged = false;
		if (m_min.ugt(b.m_min)) {
			hasChanged = true;
			m_min = b.m_min;
		}
		if (m_max.ult(b.m_max)) {
			hasChanged = true;
			m_max = b.m_max;
		}
		return hasChanged;
	}

	bool restrictMin(uint64_t _min) {
		if (m_min.ult(APInt(m_min.getBitWidth(), _min))) {
			m_min = _min;
			return true;
		}
		return false;
	}

	bool restrictMax(uint64_t _max) {
		APInt _temp(m_max);
		if (_temp.getBitWidth() < 64)
			_temp = _temp.zext(64);
		if (_temp.ugt(APInt(64, _max))) {
			m_max = _max;
			return true;
		}
		return false;
	}

	bool makeEmpty() {
		if (empty())
			return false;
		m_min = 1;
		m_max = 0;
		return true;
	}

	void print(llvm::raw_ostream &os) const {
		if (unique())
			os << value();
		else
			os << "[" << m_min << "-" << m_max << "]";
	}

	bool operator==(ValueSet &b) const {
		return (m_min == b.m_min && m_max == b.m_max)
					 || (empty() && b.empty());
	}
	bool operator!=(ValueSet &b) const { return !(*this == b); }

public:
	static ValueSet createFullRange(unsigned bits) {
		return ValueSet(0, APInt::getMaxValue(bits));
	}
	static ValueSet createFullRange(ref<Expr> e) {
		return ValueSet(0, APInt::getMaxValue(e->getWidth()));
	}
	static ValueSet createEmptyRange(unsigned bits) {
		return ValueSet(bits);
	}
	static ValueSet createNegativeRange(unsigned bits) {
		return ValueSet(((uint64_t)1) << (bits-1), APInt::getMaxValue(bits));
	}
	static ValueSet createNonNegativeRange(unsigned bits) {
		APInt _max(bits, ((uint64_t) 1) << (bits-1));
		return ValueSet(0, ~_max);
	}

public:
	ValueSet add(const ValueSet &b) const
	{
		APInt a_min(m_min), b_min(b.m_min);
		APInt a_max(m_max), b_max(b.m_max);
		APInt _min(a_min.zext(getWidth()+1) + b_min.zext(getWidth()+1));
		APInt _max(a_max.zext(getWidth()+1) + b_max.zext(getWidth()+1));
		bool overflowMin = _min.ugt(APInt::getSignedMaxValue(getWidth()+1));
		bool overflowMax = _max.ugt(APInt::getSignedMaxValue(getWidth()+1));
		bool overflow = overflowMin ^ overflowMax;
		if (_min == _max)
			return ValueSet(
				_min.trunc(getWidth()), _max.trunc(getWidth()));

		// XXX UGH lossy on overflow; we really need a disjoint (doughnut) set
		return ValueSet(
			overflow ? APInt(getWidth(), 0) : m_min + b.m_min,
			overflow ?
				APInt::getMaxValue(getWidth()) : m_max+b.m_max);
	}

	ValueSet sub(const ValueSet &b) const
	{
		APInt a_min(m_min), b_min(b.m_min);
		APInt a_max(m_max), b_max(b.m_max);
		APInt _min(a_min.zext(getWidth()+1) - b_max.zext(getWidth()+1));
		APInt _max(a_max.zext(getWidth()+1) - b_min.zext(getWidth()+1));
		bool overflowMin = _min.ugt(APInt::getSignedMaxValue(getWidth()+1));
		bool overflowMax = _max.ugt(APInt::getSignedMaxValue(getWidth()+1));
		bool overflow = overflowMin ^ overflowMax;
		if (_min == _max)
			return ValueSet(
				_min.trunc(getWidth()), _max.trunc(getWidth()));

		// XXX UGH lossy on overflow; we really need a disjoint (doughnut) set
		return ValueSet(
			overflow ? APInt(getWidth(), 0) : m_min - b.m_max,
			overflow ?
				APInt::getMaxValue(getWidth()) : m_max - b.m_min);
	}

	ValueSet mul(const ValueSet &b) const
	{
		APInt _max(m_max * b.m_max);
		// XXX should this be sdiv or udiv?
		bool overflow;

		overflow = m_max != 0 && b.m_max != 0 &&
			(_max.udiv(m_max) != b.m_max ||
			 _max.udiv(b.m_max) != m_max);

		return ValueSet(
			overflow ? APInt(getWidth(), 0) : m_min * b.m_min,
			overflow ? APInt::getMaxValue(getWidth()) : _max);
	}

	/// bitwise NOT = (~max, ~min)
	ValueSet flip() const { return ValueSet(~m_max, ~m_min); }

	ValueSet bit_and(const ValueSet &b) const {
		if (unique() && b.unique()) {
			APInt result(m_min & b.m_min);
			return ValueSet(result, result);
		}
		APInt _min(
			getWidth(),
			minAND(	m_min.getZExtValue(),
				m_max.getZExtValue(),
				b.m_min.getZExtValue(),
				b.m_max.getZExtValue()));

		APInt _max(
			getWidth(),
			maxAND(	m_min.getZExtValue(),
				m_max.getZExtValue(),
				b.m_min.getZExtValue(),
				b.m_max.getZExtValue()));
		return ValueSet(_min, _max);
	}

	ValueSet bit_or(const ValueSet &b) const
	{
		if (unique() && b.unique()) {
			APInt result(m_min | b.m_min);
			return ValueSet(result, result);
		}
		APInt _min(
			getWidth(),
			minOR(	m_min.getZExtValue(),
				m_max.getZExtValue(),
				b.m_min.getZExtValue(),
				b.m_max.getZExtValue()));
		APInt _max(
			getWidth(),
			 maxOR(	m_min.getZExtValue(),
			 	m_max.getZExtValue(),
				b.m_min.getZExtValue(),
				b.m_max.getZExtValue()));
		return ValueSet(_min, _max);
	}

	ValueSet bit_xor(const ValueSet &b) const {
		assert(unique() && b.unique() && "ranges must be unique");
		APInt result(m_min ^ b.m_min);
		return ValueSet(result, result);
	}

	// extract lowest 'bits' beginning at offset (0 is right-most bit)
	ValueSet extract(unsigned offset, unsigned bits) const {
		assert(unique());
		assert(offset + bits <= getWidth() && "out of range");
		APInt result(m_min.lshr(offset));
		result = result.trunc(bits);
		return ValueSet(result, result);
	}
	ValueSet concat(const ValueSet &b) const {
		assert(unique() && b.unique());
		APInt result(m_min);
		result = result.zext(getWidth()+b.getWidth());
		result <<= b.getWidth();
		APInt right(b.m_min);
		right = right.zext(getWidth()+b.getWidth());
		result |= right;
		return ValueSet(result, result);
	}
	ValueSet join(const ValueSet &b) const {
		APInt _min(m_min), _max(m_max);
		if (m_min.ugt(b.m_min))
			_min = b.m_min;
		if (m_max.ult(b.m_max))
			_max = b.m_max;
		return ValueSet(_min, _max);
	}
	ValueSet sext(unsigned bits) const {
		APInt _min(m_min);
		APInt _max(m_max);
		return ValueSet(_min.sext(bits), _max.sext(bits));
	}
	ValueSet urem(const ValueSet &b) const {
		assert(unique() && b.unique());
		APInt temp(m_min.urem(b.m_min));
		return ValueSet(temp, temp);
	}
	ValueSet srem(const ValueSet &b) const {
		assert(unique() && b.unique());
		APInt temp(m_min.srem(b.m_min));
		return ValueSet(temp, temp);
	}
	ValueSet udiv(const ValueSet &b) const {
		assert(unique() && b.unique());
		APInt temp(m_min.udiv(b.m_min));
		return ValueSet(temp, temp);
	}
	ValueSet sdiv(const ValueSet &b) const {
		assert(unique() && b.unique());
		APInt temp(m_min.sdiv(b.m_min));
		return ValueSet(temp, temp);
	}

	ValueSet shl(const ValueSet &b) const
	{
		assert(b.unique());
		if (unique()) {
			// known shift of known value
			APInt temp(m_min.shl(b.m_min));
			return ValueSet(temp, temp);
		}
		APInt temp(m_max.shl(b.m_min));

		// high bits lost
		if (temp.lshr(b.m_min).ult(m_max))
#if 0
// !!! FIXME: problems occur when losing high bits on min (e.g., 65-122 << 7)
// temp = APInt::getMaxValue(getWidth()).shl(b.m_min);
// if (temp.ult(m_min.shl(b.m_min))) // overflow
#endif
			return createFullRange(getWidth());
		else
			return ValueSet(m_min.shl(b.m_min), temp);
	}

	// left shift where right-most bits are unknown (e.g., reversing a right shift)
	ValueSet shl_unknown(const ValueSet &b) const
	{
		assert(b.unique());

		APInt lsb = (b.m_min == 0)
			? APInt(getWidth(), 0)
			: APInt::getMaxValue(b.m_min.getLimitedValue(64));

		if (lsb.getBitWidth() < getWidth())
			lsb = lsb.zext(getWidth());

		if (unique()) { // known shift of known value
			APInt temp(m_min.shl(b.m_min));
			return ValueSet(temp, temp + lsb);
		}

		APInt temp(m_max.shl(b.m_min));
		if (temp.lshr(b.m_min).ult(m_max)) // high bits lost
#if 0
		temp = APInt::getMaxValue(getWidth()).shl(b.m_min);
		if (temp.ult(m_min.shl(b.m_min))) // overflow
#endif
			return createFullRange(getWidth());
		else
			return ValueSet(m_min.shl(b.m_min), temp + lsb);
	}

	ValueSet ashr(const ValueSet &b) const {
		assert(unique() && b.unique());
		APInt _temp(m_min.ashr(b.m_min));
		return ValueSet(_temp, _temp);
	}
	ValueSet lshr(const ValueSet &b) const {
		assert(unique() && b.unique());
		APInt _temp(m_min.lshr(b.m_min));
		return ValueSet(_temp, _temp);
	}

	bool slt(const ValueSet &b) const {
		assert(unique() && b.unique());
		return m_min.slt(b.m_min);
	}
	bool sle(const ValueSet &b) const {
		assert(unique() && b.unique());
		return m_min.sle(b.m_min);
	}
};

class RangeSimplifier
{
private:
	typedef std::map<ref<Expr>, ValueSet> RangeMap;
	RangeMap currentRanges;

	typedef std::vector<ValueSet> ArrayValues;
	typedef std::map<const Array*, ArrayValues, ArrayLT> ArrayMap;
	typedef std::map<const Array*, std::set<ref<Expr> >, ArrayLT> OrderedReadMap;
	/// stores known ranges of symbolic and concrete arrays
	ArrayMap arrayRanges;
	/// concatenation chains of reads that should be considered as a whole when
	/// choosing a counterexample
	OrderedReadMap orderedReads;

	typedef std::pair<unsigned,ref<Expr> > ExprDepth;
	struct LessDepth {
		bool operator() (const ExprDepth &a, const ExprDepth &b) const
		{
			// compare ref<Expr> to break ties
			return (a.first == b.first)
				? (a.second < b.second) // tie
				: (a.first < b.first); // compare depth
		}
	};

	typedef std::priority_queue<
		ExprDepth,
		std::deque<ExprDepth>,
		LessDepth> DepthQueue;

	// priority queue of nodes to be (re)visited (greatest depth first)
	DepthQueue pendingQueue;
	// exprs (redundant with pendingQueue) indexed by ref, not priority
	std::set<ref<Expr> > pendingNodes;

	// nodes that haven't changed since they were last visited;
	// must be disjoint from pendingNodes
	std::set<ref<Expr> > visitedNodes;

	typedef std::map<ref<Expr>, std::set<ref<Expr> > > DependentMap;

	// maps each expr to a set of exprs that depend on it
	DependentMap exprDependents;

	typedef std::vector<std::set<ref<Expr> > > ArrayDependentSet;
	typedef std::map<const Array*, ArrayDependentSet, ArrayLT> ArrayDependentMap;

	// maps each array index to a set of exprs that depend on it
	ArrayDependentMap arrayDependents;

	typedef std::map<ref<Expr>, unsigned> DepthMap;

	// maps each expr to its maximum depth in any subtree
	DepthMap exprDepths;

	bool contradiction;
	bool unsupported; // DEBUG REMOVE

	double startTime;
	bool timeout;

public:
	RangeSimplifier()
	: contradiction(false)
	, unsupported(false)
	, timeout(false)
	{
		startTime = util::estWallTime();
	}

	/// algorithm entry point
	bool run(const Query& query, bool &hasSolution);

	/// find counterexample after running simplification algorithm
	bool findCex(const Query& query, Assignment& a);

	/// dump a counter-example to stderr
	void printCex(
		const Array* arr,
		const std::vector<unsigned char> &values) const;

private:
	/// prepare a constraint, assuming it to be true
	void prepareConstraint(ref<Expr> e);

	/// prepare a query to determining its validity
	void prepareQuery(ref <Expr> e);

	/// add an expression to the queue
	void pushExpr(ref<Expr> e);

	/// add an expression's dependents to the queue (except parent if given)
	void pushDependentsOf(ref<Expr> e, ref<Expr> parent = ref<Expr>());

	/// add an array's readers to the queue (except parent if given)
	void pushReadsOf(
		const Array* arr, unsigned index, ref<Expr> parent = ref<Expr>());

	/// wrapper for managing graph algorithm queues/sets
	bool processExpr(ref<Expr> e, ref<Expr> parent = ref<Expr>(), unsigned depth = 0);

	/// actual recursion for processing constraints/queries
	bool processExprInternal(ref<Expr> e, unsigned depth);

private:
	ValueSet& getRange(ref<Expr> e) {
		RangeMap::iterator it = currentRanges.find(e);
		if (it == currentRanges.end())
			return currentRanges.insert(
				std::make_pair(e, ValueSet::createFullRange(e))).first->second;
		else
			return it->second;
	}

	void setRange(ref<Expr> e, ValueSet &range) {
		currentRanges[e] = range;
	}

	bool setValue(ref<Expr> e, uint64_t val) {
		ValueSet range(e->getWidth(), val);
		RangeMap::iterator it = currentRanges.find(e);
		if (it == currentRanges.end()
				|| range != it->second) {
			currentRanges[e] = range;
			return true;
		} else
			return false;
	}

	void initArray(const Array* arr);

private:
	static bool isReadExprAtOffset(
		ref<Expr> e, const ReadExpr *base, ref<Expr> offset);
	static const ReadExpr* isOrderedRead(ref<Expr> e, int &stride);
};

void RangeSimplifier::initArray(const Array* arr)
{
	ArrayMap::iterator it = arrayRanges.find(arr);

	if (it != arrayRanges.end()) return;

	// initialize array range
	ArrayValues av(arr->mallocKey.size);
	for (unsigned i = 0; i < arr->mallocKey.size; i++) {
		if (!arr->isConstantArray()) {
			// initialize unused array indexes to empty
			av[i] = ValueSet::createEmptyRange(Expr::Int8);
			continue;
		}

		av[i] = ValueSet(
			Expr::Int8,
			arr->getValue(i)->getZExtValue(Expr::Int8));
	}
	it = arrayRanges.insert(std::make_pair(arr, av)).first;
}

bool RangeSimplifier::findCex(const Query& query, Assignment& a)
{
	// XXX are we better off assigning all concatenated reads
	// (in prioritized order) before all singular reads,
	// or should we interleave the two?
	std::set<ref<Expr> >				assignmentQueue;
	std::vector< std::vector<unsigned char> >	values;
	std::vector< const Array* >			objects;
	bool						unknown = false;
	// true = value assigned; false = needs a value
	std::map<const Array*, std::vector<bool>, ArrayLT> objectStatus;

	// initialize objectStatus and values
	foreach(it, a.freeBegin(), a.freeEnd()) {
		const Array* arr = *it;
		objectStatus.insert(
			std::make_pair(
				arr,
				std::vector<bool>(arr->mallocKey.size, false)));
		values.push_back(
			std::vector<unsigned char>(arr->mallocKey.size, 0));
		objects.push_back(arr);
	}

	// initialize assignmentQueue to contain all ordered reads
	foreach(it, orderedReads.begin(), orderedReads.end())
		assignmentQueue.insert(it->second.begin(), it->second.end());

	// strategy:
	// loop through all ordered reads and unassigned values and find the
	// ordered read/unassigned value with the lowest range size
	// (relative to the max range size for that width).
	// Assign in increasing order of possible ranges.
	// Terminate early if we get a contradiction (oops).
	//
	// XXX this code sucks. Refactor it -AJR
	while(true) {
		// actual range of values will be in [0,1]
		double currentMin = 1.1;
		bool minIsRead = false;
		ref<Expr> minRead;
		const Array* minArray = NULL;
		unsigned minIdx = 0;

		// find smallest remaining range
		foreach (ait,
			assignmentQueue.begin(),
			assignmentQueue.end())
		{
			double	rs;

			if (currentMin == 0.0)
				break;

			ref<Expr> curRead = *ait;

			ValueSet &range(getRange(curRead));
			assert(!range.empty());

			rs = range.rel_size();
			if (rs < currentMin) {
				currentMin = rs;
				minIsRead = true;
				minRead = curRead;
			}
		}

		for (unsigned objIdx = 0; objIdx < objects.size(); objIdx++) {
			const Array* arr = objects[objIdx];

			// array fully assigned
			if (objectStatus.find(arr) == objectStatus.end())
				continue;

			ArrayMap::const_iterator rit = arrayRanges.find(arr);
			if (rit == arrayRanges.end()) {
				// array not used
				// arbitrarily set to zeroes
				values[objIdx].assign(arr->mallocKey.size, 0);
				objectStatus.erase(arr);
				continue;
			}

			const ArrayValues &av = rit->second;

			// is this array fully assigned?
			bool arrayDone = true;
			for (unsigned i = 0; i < arr->mallocKey.size; i++) {
				// index already assigned
				if (objectStatus[arr][i])
					continue;

				// arbitrarily set "don't care" indexes to 0
				if (av[i].empty()) {
					values[objIdx][i] = 0;
					objectStatus[arr][i] = true;
					continue;
				}

				if (av[i].unique()) {
					values[objIdx][i] = av[i].value();
					objectStatus[arr][i] = true;
					continue;
				}

				arrayDone = false;
				double rs = av[i].rel_size();
				if (rs < currentMin) {
					currentMin = rs;
					minIsRead = false;
					minArray = arr;
					minIdx = i;
				}
			}

			if (arrayDone)
				objectStatus.erase(arr);
		}

		if (currentMin == 1.1) // all arrays have been assigned
			break;

		if (minIsRead) {
			ValueSet &range = getRange(minRead);
			assignmentQueue.erase(minRead);

			if (range.unique())
				continue;

			// guess lowest
			range.monoSet(ValueSet(range.getWidth(), range.min()));
			pushExpr(minRead);

			// revisit dependent expressions
			pushDependentsOf(minRead);
			unknown = true; // we guessed
		} else {
			ArrayMap::iterator rit = arrayRanges.find(minArray);
			assert(rit != arrayRanges.end());
			ArrayValues &av = rit->second;

			unsigned arrayIdx = 0;
			for (unsigned i = 0; i < objects.size(); i++) {
				if (objects[i] == minArray) {
					arrayIdx = i;
					break;
				}
			}

			// guess lowest value
			unknown |= av[minIdx].monoSet(
				ValueSet(av[minIdx].getWidth(),
				av[minIdx].min()));

			values[arrayIdx][minIdx] = av[minIdx].value();
			objectStatus[minArray][minIdx] = true;

			// revisit reads of this array
			pushReadsOf(minArray, minIdx);
		}

		bool hasSolution;
		bool success = run(query, hasSolution);
		if (!success || !hasSolution) {
			// we made a contradiction... damnit
			return false;
		}
	}

	if (unknown == false) {
		/* success! dump to assignment */
		for (unsigned i = 0; i < objects.size(); i++)
			a.bindFree(objects[i], values[i]);
	}

	return !unknown;
}

void RangeSimplifier::printCex(
	const Array* arr,
	const std::vector<unsigned char> &values) const

{
	ArrayMap::const_iterator it = arrayRanges.find(arr);
	if (it == arrayRanges.end()) { // array not used
		std::cerr << arr->name << " = [";
		for (unsigned i = 0; i < arr->mallocKey.size; i++) {
			if (i) std::cerr << ",";
			std::cerr << 0;
		}
		std::cerr << "]\n";
		return;
	}
	const ArrayValues &av = it->second;

	std::cerr << arr->name << " = [";
	for (unsigned i = 0; i < arr->mallocKey.size; i++) {
		if (i) std::cerr << ",";
		if (av[i].empty())
			std::cerr << 0;
		else if (av[i].unique())
			std::cerr << av[i].value();
		else {
			std::cerr << av[i].min() << "-" << av[i].max();
			if (!values.empty())
				std::cerr << " (" << (unsigned)values[i] << ")";
		}
	}
	std::cerr << "]\n";
}

// based on ExprPPrinter.cpp: isReadExprAtOffset
bool RangeSimplifier::isReadExprAtOffset(
	ref<Expr> e, const ReadExpr *base, ref<Expr> offset)
{
	const ReadExpr *re = dyn_cast<ReadExpr>(e.get());

	// right now, all Reads are byte reads but some
	// transformations might change this
	if (!re || (re->getWidth() != Expr::Int8))
		return false;

	// Check if the index follows the stride.
	// FIXME: How aggressive should this be simplified. The
	// canonicalizing builder is probably the right choice, but this
	// is yet another area where we would really prefer it to be
	// global or else use static methods.
	return SubExpr::create(re->index, base->index) == offset;
}

// based on ExprPPrinter.cpp: hasOrderedReads()
const ReadExpr* RangeSimplifier::isOrderedRead(ref<Expr> e, int &stride)
{
	assert(e->getKind() == Expr::Concat);

	const ReadExpr *base = dyn_cast<ReadExpr>(e->getKid(0));

	// right now, all Reads are byte reads but some
	// transformations might change this
	if (!base || base->getWidth() != Expr::Int8)
		return NULL;

	for (unsigned i = 0; i < 2; i++) {
		stride = i ? 1 : -1;

		ref<Expr> temp = e;

		// Get stride expr in proper index width.
		Expr::Width idxWidth = base->index->getWidth();
		ref<Expr> strideExpr = ConstantExpr::alloc(stride, idxWidth);
		ref<Expr> offset = ConstantExpr::create(0, idxWidth);

		temp = temp->getKid(1);

		bool toContinue = false;
		// concat chains are unbalanced to the right
		while (temp->getKind() == Expr::Concat) {
			offset = AddExpr::create(offset, strideExpr);
			if (!isReadExprAtOffset(temp->getKid(0), base, offset)) {
				toContinue = true;
				break;
			}

			temp = temp->getKid(1);
		}
		if (toContinue)
			continue;

		offset = AddExpr::create(offset, strideExpr);
		if (!isReadExprAtOffset(temp, base, offset))
			continue;

		if (stride == -1)
			return cast<ReadExpr>(temp.get());
		else
			return base;
	}
	return NULL;
}

bool RangeSimplifier::run(const Query& query, bool &hasSolution)
{
	contradiction = false;
	foreach (it, query.constraints.begin(), query.constraints.end()) {
		ref<Expr> c = *it;
		prepareConstraint(c);
	}

	prepareQuery(query.expr);
	ValueSet &queryRange = getRange(query.expr);

	while (!pendingQueue.empty()) {
		ref<Expr> e = pendingQueue.top().second;
		pendingQueue.pop();
		if (!pendingNodes.erase(e)) // already processed this expression
			continue;

		processExpr(e);

		if (contradiction) { // we're done
			hasSolution = false;
			return true;
		} else if (unsupported) {
#ifdef FRS_DEBUG
			std::cerr << "FastRangeSolver UNSUPPORTED\n";
#endif
			return false;
		} else if (timeout)
			return false;
	}

	assert(queryRange.unique() && !queryRange.value());

	hasSolution = true;
	return true;
}

void RangeSimplifier::pushExpr(ref<Expr> e)
{
	visitedNodes.erase(e);

	if (pendingNodes.find(e) != pendingNodes.end()) return;

	pendingQueue.push(std::make_pair(exprDepths[e], e));
	pendingNodes.insert(e);
}

void RangeSimplifier::pushDependentsOf(ref<Expr> e, ref<Expr> parent)
{
	DependentMap::iterator dmit = exprDependents.find(e);
	if (dmit == exprDependents.end())
		return;

	std::set<ref<Expr> > &dependents = dmit->second;
	foreach (it, dependents.begin(), dependents.end()) {
		ref<Expr> d = *it;

		// don't re-add parent to queue
		if (!parent.isNull() && d == parent)
			continue;

		pushExpr(d);
	}
}

void RangeSimplifier::pushReadsOf(
	const Array* arr, unsigned index, ref<Expr> parent)
{
	// revisit array's dependents
	ArrayDependentMap::iterator adit = arrayDependents.find(arr);
	if (adit == arrayDependents.end())
		return;

	// add all dependent reads to queue
	std::set<ref<Expr> > &dependents = adit->second[index];
	foreach (it, dependents.begin(), dependents.end()) {
		ref<Expr> d = *it;
		// don't re-add parent to queue
		if (!parent.isNull() && d == parent)
			continue;

		pushExpr(d);
	}
}

void RangeSimplifier::prepareConstraint(ref<Expr> e)
{
	assert(e->getWidth() == Expr::Bool && "Non-boolean constraint");
	ValueSet &range = getRange(e);
	range.monoSet(ValueSet(1, true));

	unsigned &depth = exprDepths[e];

	// by default, make sure we have a higher priority than the query
	if (!depth) depth = 1;

	pushExpr(e);
}

void RangeSimplifier::prepareQuery(ref <Expr> e)
{
	assert(e->getWidth() == Expr::Bool && "Non-boolean constraint");

	ValueSet &range = getRange(e);
	range.monoSet(ValueSet(1, false));

	pushExpr(e);
}

#define IF_CHANGE(_change, _expr, _or) { \
	bool ___change___ = (_change); \
	if (___change___) visitedNodes.erase((_expr)); \
	(_or) |= ___change___; \
	}

bool RangeSimplifier::processExpr(
	ref<Expr> e, ref<Expr> parent, unsigned depth)
{
	// mark parent (caller) as a dependent expr
	if (!parent.isNull())
		exprDependents[e].insert(parent);

	// update depth information
	if (depth > exprDepths[e])
		exprDepths[e] = depth;

	// no changes since last visit, so prune
	if (visitedNodes.find(e) != visitedNodes.end())
		return false;

	bool hasChanged = processExprInternal(e, depth);

	visitedNodes.insert(e);

	if (hasChanged && !contradiction)
		pushDependentsOf(e, parent);

	return hasChanged;
}

/* XXX REWRITE ME */
bool RangeSimplifier::processExprInternal(ref<Expr> e, unsigned depth)
{
	bool hasEverChanged = false, hasChanged = true;
	ValueSet &range = getRange(e);

	if (e->getWidth() > Expr::Int64) {
		klee_warning_once(0, "FastRangeSolver: max expression width is 64 bits");
		unsupported = true;
		return false;
	}

	while (hasChanged && !contradiction && !unsupported && !timeout) {
		hasChanged = false;

		switch (e->getKind()) {
		case Expr::Constant: {
			ref<ConstantExpr> ce = cast<ConstantExpr>(e);
			range.monoSet(ValueSet(ce->getWidth(), ce->getZExtValue()));
			break;
		}

		// Special

		case Expr::NotOptimized: {
			ref<NotOptimizedExpr> noe = cast<NotOptimizedExpr>(e);
			ValueSet &srcRange = getRange(noe->src);

			hasChanged |= processExpr(noe->src, e, depth+1);

			IF_CHANGE(srcRange.monoSet(range), noe->src, hasChanged)
			hasChanged |= range.monoSet(srcRange);
			break;
		}

		case Expr::Read: {
			ref<ReadExpr> re = cast<ReadExpr>(e);
			const Array *arr = re->updates.root;

			ValueSet &offsetRange = getRange(re->index);

			hasChanged |= processExpr(re->index, e, depth+1);

			initArray(arr);

			ArrayMap::iterator it = arrayRanges.find(arr);
			assert(it != arrayRanges.end());

			ArrayValues &avRef = it->second;
			ArrayValues av(avRef); // need a local copy for registering read updates

			std::vector<bool> updated(arr->mallocKey.size, false);

			ArrayDependentMap::iterator adit = arrayDependents.find(arr);
			if (adit == arrayDependents.end())
				adit =
					arrayDependents.insert(std::make_pair(arr,
																								ArrayDependentSet(arr->mallocKey.size))).first;

			// handle symbolic offset writes
			std::stack<const UpdateNode*> updates;
			for (const UpdateNode *un = re->updates.head; un; un = un->next)
				updates.push(un);

			while (!updates.empty()) {
				const UpdateNode *un = updates.top();
				updates.pop();

				ValueSet &indexRange = getRange(un->index);
				ValueSet &valueRange = getRange(un->value);
				hasChanged |= processExpr(un->index, e, depth+1);
				hasChanged |= processExpr(un->value, e, depth+1);

				if (indexRange.unique()) {
					// XXX is this the right behavior, or does this indicate a bigger
					// problem?
					if (indexRange.value() >= arr->mallocKey.size) {
						contradiction = true;
						break;
					}
					av[indexRange.value()] = valueRange;
					updated[indexRange.value()] = true;
				} else {
					for (unsigned i = 0; i < arr->mallocKey.size; i++) {
						if (!indexRange.contains(i))
							continue;
						if (av[i].empty()) // never referenced; uninitialized
							av[i] = ValueSet::createFullRange(Expr::Int8);
						av[i].unionSet(valueRange);
						updated[i] = true;
					}
				}
			}

			if (offsetRange.unique()) { // known offset read
				unsigned i = offsetRange.value();
				assert(i < arr->mallocKey.size && "out of bounds read");

				// mark this read as dependent on this array index
				adit->second[i].insert(e);

				assert(i < av.size() && "PANIC: out of bounds read");
				if (av[i].empty()) // never referenced; uninitialized
					av[i] = avRef[i] = range;
				else {
					if (!updated[i]) { // we're reading from the array, not an update
						bool hasChangedLocal = avRef[i].monoSet(range);

						if (hasChangedLocal)
							pushReadsOf(arr, i, e); // add all dependent reads to queue

						hasChanged |= hasChangedLocal;
					}
					av[i].monoSet(range);

					if (av[i].empty())
						contradiction = true;
				}
				hasChanged |= range.monoSet(av[i]);
			}
			// is there anything else we can do if we don't know the read offset?
			else {
				int idxMin = -1, idxMax = -1;
				unsigned char valMin = 255, valMax = 0;
				for (unsigned i = 0; i < av.size(); i++) {
					if (av[i].empty()) {
						av[i] = avRef[i] = ValueSet::createFullRange(Expr::Int8);
						hasChanged = true;
					}
					if (av[i].intersects(range)) {

						// mark this read as (potentially) dependent on this array index
						adit->second[i].insert(e);

						if (idxMin == -1)
							idxMin = i;
						idxMax = i;

						if (offsetRange.contains(i)) {
							if (av[i].min() < valMin)
								valMin = av[i].min();
							if (av[i].max() > valMax)
								valMax = av[i].max();
						}
					}
				}
				IF_CHANGE(offsetRange.monoSet(
					ValueSet(
						APInt(re->index->getWidth(), idxMin),
						APInt(re->index->getWidth(), idxMax))),
						re->index, hasChanged)
				hasChanged |= range.monoSet(
					ValueSet(
						APInt(re->getWidth(), valMin),
						APInt(re->getWidth(), valMax)));
			}

			break;
		}

		case Expr::Select: {
			ref<SelectExpr> se = cast<SelectExpr>(e);

			ValueSet &condRange = getRange(se->cond);
			ValueSet &trueRange = getRange(se->trueExpr);
			ValueSet &falseRange = getRange(se->falseExpr);

			hasChanged |= processExpr(se->cond, e, depth+1);
			hasChanged |= processExpr(se->trueExpr, e, depth+1);
			hasChanged |= processExpr(se->falseExpr, e, depth+1);

			if (condRange.unique()) {
				if (condRange.value()) { // true
					hasChanged |= range.monoSet(trueRange);
					IF_CHANGE(trueRange.monoSet(range), se->trueExpr, hasChanged)
				} else { // false
					hasChanged |= range.monoSet(falseRange);
					IF_CHANGE(falseRange.monoSet(range), se->falseExpr, hasChanged)
				}
			} else // cond unknown, narrow range to union of true/false ranges
				hasChanged |= range.monoSet(trueRange.join(falseRange));

			if (range.superset(trueRange) && !range.intersects(falseRange)) {
				// range contains trueRange but not falseRange, so cond must be true
				hasChanged |= range.monoSet(trueRange);
				IF_CHANGE(trueRange.monoSet(range), se->trueExpr, hasChanged)
				IF_CHANGE(condRange.monoSet(ValueSet(1, true)), se->cond, hasChanged)
			} else if (range.superset(falseRange) && !range.intersects(trueRange)) {
				// range contains falseRange but not trueRange, so cond must be false
				hasChanged |= range.monoSet(falseRange);
				IF_CHANGE(falseRange.monoSet(range), se->falseExpr, hasChanged)
				IF_CHANGE(condRange.monoSet(ValueSet(1, false)), se->cond, hasChanged)
			}

			break;
		}
		case Expr::Concat: {

			ref<ConcatExpr> ce = cast<ConcatExpr>(e);

			int stride = 0;
			if (const ReadExpr *re = isOrderedRead(ce, stride)) {
				std::set<ref<Expr> > &reads = orderedReads[re->updates.root];

				// check whether this is a subexpression in an ordered read we've already
				// registered
				std::set<ref<Expr> >::iterator rit = reads.begin(), rie = reads.end();
				for (; rit != rie; ++rit) {
					ref<Expr> r = *rit;
					int stride2 = 0;
					const ReadExpr *re2 = isOrderedRead(r, stride2);
					if (re->updates.root == re2->updates.root && re->index == re2->index
							&& stride == stride2 && ce->getWidth() <= r->getWidth())
						break;
				}
				if (rit == rie)
					reads.insert(ce);
			}

			ValueSet &leftRange = getRange(ce->getLeft());
			ValueSet &rightRange = getRange(ce->getRight());

			hasChanged |= processExpr(ce->getLeft(), e, depth+1);
			hasChanged |= processExpr(ce->getRight(), e, depth+1);

			if (range.unique()) {
				IF_CHANGE(leftRange.monoSet(range.extract(ce->getWidth()-ce->getLeft()->getWidth(), ce->getLeft()->getWidth())),
									ce->getLeft(), hasChanged)
				IF_CHANGE(rightRange.monoSet(range.extract(0, ce->getRight()->getWidth())),
									ce->getRight(), hasChanged)
			} else if (range.max() <
								 APInt::getMaxValue(ce->getRight()->getWidth()).getZExtValue()) {
				// if range fits entirely within right_width bits, restrict left to 0
				IF_CHANGE(leftRange.restrictMax(0), ce->getLeft(), hasChanged)
			}

			if (leftRange.unique() && rightRange.unique()) {
				hasChanged |= range.monoSet(leftRange.concat(rightRange));
			}

			hasChanged |= range.restrictMin((leftRange.min() << ce->getRight()->getWidth()) + rightRange.min());
			hasChanged |= range.restrictMax((leftRange.max() << ce->getRight()->getWidth()) + rightRange.max());

			break;
		}

		case Expr::Extract: {

			ref<ExtractExpr> ee = cast<ExtractExpr>(e);

			ValueSet &exprRange = getRange(ee->expr);
			hasChanged |= processExpr(ee->expr, e, depth+1);

			if (range.unique()) {
				// XXX is there anything we can propagate down?
			}

			if (!ee->offset) // LSB read
				IF_CHANGE(exprRange.restrictMin(range.min()), ee->expr, hasChanged)

			if (ee->offset == (ee->expr->getWidth() - ee->width)) // MSB read
				IF_CHANGE(exprRange.restrictMax((range.max() << ee->offset) + APInt::getMaxValue(ee->offset).getZExtValue()), ee->expr, hasChanged)

			// if we know the expression, just extract the bits we want
			if (exprRange.unique()) {
				hasChanged |= range.monoSet(exprRange.extract(ee->offset, ee->width));
			// if
			} else if (ee->offset == 0 &&
								 exprRange.max() <
									 APInt::getMaxValue(ee->offset+ee->width).getZExtValue())
				hasChanged |= range.restrictMin(exprRange.min());

			hasChanged |= range.restrictMax(exprRange.max());

			break;
		}

		// Casting

		case Expr::ZExt: {
			ref<CastExpr> ce = cast<CastExpr>(e);

			ValueSet &srcRange = getRange(ce->src);
			hasChanged |= processExpr(ce->src, e, depth+1);

			hasChanged |= range.monoSet(srcRange);
			IF_CHANGE(srcRange.monoSet(range), ce->src, hasChanged)
			break;
		}

		case Expr::SExt: {
			ref<CastExpr> ce = cast<CastExpr>(e);

			ValueSet &srcRange = getRange(ce->src);

			hasChanged |= processExpr(ce->src, e, depth+1);

			hasChanged |= range.monoSet(srcRange.sext(ce->getWidth()));

			if (range.unique()) {
				IF_CHANGE(srcRange.monoSet(range.extract(0, ce->src->getWidth())),
									ce->src, hasChanged)
			}

			break;
		}

		// Binary

		case Expr::Add: {
			BinaryExpr *be = cast<BinaryExpr>(e);

			ValueSet &leftRange = getRange(be->left);
			ValueSet &rightRange = getRange(be->right);

			hasChanged |= processExpr(be->left, e, depth+1);
			hasChanged |= processExpr(be->right, e, depth+1);

			hasChanged |= range.monoSet(leftRange.add(rightRange));
			IF_CHANGE(leftRange.monoSet(range.sub(rightRange)), be->left, hasChanged)
			IF_CHANGE(rightRange.monoSet(range.sub(leftRange)), be->right, hasChanged)

			break;
		}

		case Expr::Sub: {
			BinaryExpr *be = cast<BinaryExpr>(e);

			ValueSet &leftRange = getRange(be->left);
			ValueSet &rightRange = getRange(be->right);

			hasChanged |= processExpr(be->left, e, depth+1);
			hasChanged |= processExpr(be->right, e, depth+1);

			hasChanged |= range.monoSet(leftRange.sub(rightRange));
			IF_CHANGE(leftRange.monoSet(range.add(rightRange)), be->left, hasChanged)
			IF_CHANGE(rightRange.monoSet(leftRange.sub(range)), be->right, hasChanged)

			break;
		}

		case Expr::And: {
			BinaryExpr *be = cast<BinaryExpr>(e);

			ValueSet &leftRange = getRange(be->left);
			ValueSet &rightRange = getRange(be->right);

			hasChanged |= processExpr(be->left, e, depth+1);
			hasChanged |= processExpr(be->right, e, depth+1);

			hasChanged |= range.monoSet(leftRange.bit_and(rightRange));

			// special case for boolean AND
			if (e->getWidth() != Expr::Bool) break;
			if (!range.unique()) break;
			if (range.value()) { // both left and right must be true
				IF_CHANGE(leftRange.monoSet(ValueSet(1, true)), be->left, hasChanged)
				IF_CHANGE(rightRange.monoSet(ValueSet(1, true)), be->right,
									hasChanged)
			} else { // at least one of left or right must be false
				if (leftRange.unique() && leftRange.value())
					IF_CHANGE(rightRange.monoSet(ValueSet(1, false)), be->right,
										hasChanged)
				else if (rightRange.unique() && rightRange.value())
					IF_CHANGE(leftRange.monoSet(ValueSet(1, false)), be->left,
										hasChanged)
			}

			break;
		}

		case Expr::Or: {
			BinaryExpr *be = cast<BinaryExpr>(e);

			ValueSet &leftRange = getRange(be->left);
			ValueSet &rightRange = getRange(be->right);

			hasChanged |= processExpr(be->left, e, depth+1);
			hasChanged |= processExpr(be->right, e, depth+1);

			hasChanged |= range.monoSet(leftRange.bit_or(rightRange));

			// special case for boolean OR
			if (e->getWidth() != Expr::Bool) break;
			if (!range.unique()) break;
			if (range.value()) { // at least one of left or right must be true
				if (leftRange.unique() && !leftRange.value())
					IF_CHANGE(rightRange.monoSet(ValueSet(1, true)), be->right,
										hasChanged)
				else if (rightRange.unique() && !rightRange.value())
					IF_CHANGE(leftRange.monoSet(ValueSet(1, true)), be->left,
										hasChanged)
			} else { // both left and right must be false
				IF_CHANGE(leftRange.monoSet(ValueSet(1, false)), be->left, hasChanged)
				IF_CHANGE(rightRange.monoSet(ValueSet(1, false)), be->right,
									hasChanged)
			}

			break;
		}

		case Expr::Xor: {
			BinaryExpr *be = cast<BinaryExpr>(e);

			ValueSet &leftRange = getRange(be->left);
			ValueSet &rightRange = getRange(be->right);

			hasChanged |= processExpr(be->left, e, depth+1);
			hasChanged |= processExpr(be->right, e, depth+1);

			if (leftRange.unique() && rightRange.unique())
				hasChanged |= range.monoSet(leftRange.bit_xor(rightRange));
			// XOR is symmetric!
			else if (range.unique() && leftRange.unique())
				IF_CHANGE(rightRange.monoSet(range.bit_xor(leftRange)), be->right,
									hasChanged)
			else if (range.unique() && rightRange.unique())
				IF_CHANGE(leftRange.monoSet(range.bit_xor(rightRange)), be->left,
									hasChanged)

			break;
		}

		// Comparison

		case Expr::Eq: {
			BinaryExpr *be = cast<BinaryExpr>(e);

			ValueSet &leftRange = getRange(be->left);
			ValueSet &rightRange = getRange(be->right);

			hasChanged |= processExpr(be->left, e, depth+1);
			hasChanged |= processExpr(be->right, e, depth+1);

			bool update = hasChanged || leftRange.unique() || rightRange.unique() ||
											((leftRange.isFullRange() || isa<ConstantExpr>(be->left))
											 && (rightRange.isFullRange()));

			if (range.unique()) {
				if (range.value()) { // Eq is true
					IF_CHANGE(rightRange.monoSet(leftRange), be->right, hasChanged)
					IF_CHANGE(leftRange.monoSet(rightRange), be->left, hasChanged)
					assert(leftRange == rightRange);
				} else if (be->left != be->right) { // Eq is false
					// if one side is unique and happens to be the min/max of the other
					// side, remove that value from the range
					if (leftRange.unique()) {
						if (leftRange.value() == rightRange.min() && update)
							IF_CHANGE(rightRange.restrictMin(rightRange.min() + 1), be->right,
												hasChanged)
						else if (leftRange.value() == rightRange.max() && update)
							IF_CHANGE(rightRange.restrictMax(rightRange.max() - 1), be->right,
												hasChanged)
					}

					if (rightRange.unique()) {
						if (rightRange.value() == leftRange.min() && update)
							IF_CHANGE(leftRange.restrictMin(leftRange.min() + 1), be->left,
												hasChanged)
						else if (rightRange.value() == leftRange.max() && update)
							IF_CHANGE(leftRange.restrictMax(leftRange.max() - 1), be->right,
												hasChanged)
					}
				}
			}

			if (be->left == be->right)
				hasChanged |= range.monoSet(ValueSet(1, true));
			else if (leftRange.unique() && rightRange.unique()) {
				if (leftRange == rightRange)
					hasChanged |= range.monoSet(ValueSet(1, true));
				else
					hasChanged |= range.monoSet(ValueSet(1, false));
			} else if (!leftRange.intersects(rightRange)) // disjoint sets
				hasChanged |= range.monoSet(ValueSet(1, false));
			else // intersecting sets
				hasChanged |= range.monoSet(ValueSet(APInt(1, false), APInt(1, true)));

			break;
		}

		case Expr::Not: {
			ref<NotExpr> ne = cast<NotExpr>(e);

			ValueSet &exprRange = getRange(ne->expr);

			hasChanged |= processExpr(ne->expr, e, depth+1);

			hasChanged |= range.monoSet(exprRange.flip());
			IF_CHANGE(exprRange.monoSet(range.flip()), ne->expr, hasChanged);

			break;
		}

		case Expr::Ult: {
			BinaryExpr *be = cast<BinaryExpr>(e);

			ValueSet &leftRange = getRange(be->left);
			ValueSet &rightRange = getRange(be->right);

			hasChanged |= processExpr(be->left, e, depth+1);
			hasChanged |= processExpr(be->right, e, depth+1);

			bool update = hasChanged || leftRange.unique() || rightRange.unique() ||
											((leftRange.isFullRange() || isa<ConstantExpr>(be->left))
											 && (rightRange.isFullRange()));

			if (range.unique()) {
				if (range.value() && be->left != be->right && update) { // left < right
					IF_CHANGE(leftRange.restrictMax(rightRange.max() - 1), be->left,
										hasChanged)
					IF_CHANGE(rightRange.restrictMin(leftRange.min() + 1), be->right,
										hasChanged)
				} else if (!range.value()) { // left >= right
					IF_CHANGE(leftRange.restrictMin(rightRange.min()), be->left,
										hasChanged)
					IF_CHANGE(rightRange.restrictMax(leftRange.max()), be->right,
										hasChanged)
				}
			}

			if (be->left == be->right) // left == right, so !(left < right)
				hasChanged |= range.monoSet(ValueSet(1, false));
			else if (leftRange.unique() && rightRange.unique()) {
				if (leftRange.value() < rightRange.value())
					hasChanged |= range.monoSet(ValueSet(1, true));
				else
					hasChanged |= range.monoSet(ValueSet(1, false));
			} else if (leftRange.min() >= rightRange.max())
				hasChanged |= range.monoSet(ValueSet(1, false));
			else if (leftRange.max() < rightRange.min())
				hasChanged |= range.monoSet(ValueSet(1, true));

			break;
		}
		case Expr::Ule: {
			BinaryExpr *be = cast<BinaryExpr>(e);

			ValueSet &leftRange = getRange(be->left);
			ValueSet &rightRange = getRange(be->right);

			hasChanged |= processExpr(be->left, e, depth+1);
			hasChanged |= processExpr(be->right, e, depth+1);

			bool update = hasChanged || leftRange.unique() || rightRange.unique() ||
											((leftRange.isFullRange() || isa<ConstantExpr>(be->left))
											 && (rightRange.isFullRange()));

			if (range.unique()) {
				if (range.value() && be->left != be->right) { // left <= right
					IF_CHANGE(leftRange.restrictMax(rightRange.max()), be->left,
										hasChanged)
					IF_CHANGE(rightRange.restrictMin(leftRange.min()), be->right,
										hasChanged)
				} else if (!range.value() && update) { // left > right
					IF_CHANGE(leftRange.restrictMin(rightRange.min() + 1), be->left,
										hasChanged)
					IF_CHANGE(rightRange.restrictMax(leftRange.max() - 1), be->right,
										hasChanged)
				}
			}

			if (be->left == be->right) // left == right, so (left <= right)
				hasChanged |= range.monoSet(ValueSet(1, true));
			else if (leftRange.unique() && rightRange.unique()) {
				if (leftRange.value() <= rightRange.value())
					hasChanged |= range.monoSet(ValueSet(1, true));
				else
					hasChanged |= range.monoSet(ValueSet(1, false));
			} else if (leftRange.min() > rightRange.max())
				hasChanged |= range.monoSet(ValueSet(1, false));
			else if (leftRange.max() <= rightRange.min())
				hasChanged |= range.monoSet(ValueSet(1, true));

			break;
		}

		case Expr::Slt: {
			// UGH 2's complement causes an annoying range discontinuity that limits
			// what we can do here without supporting discontinuities
			BinaryExpr *be = cast<BinaryExpr>(e);

			ValueSet &leftRange = getRange(be->left);
			ValueSet &rightRange = getRange(be->right);

			hasChanged |= processExpr(be->left, e, depth+1);
			hasChanged |= processExpr(be->right, e, depth+1);

			bool update = hasChanged || leftRange.unique() || rightRange.unique() ||
											((leftRange.isFullRange() || isa<ConstantExpr>(be->left))
											 && (rightRange.isFullRange()));

			if (range.unique()) {
				if (range.value() && be->left != be->right && update) { // left < right
					if (leftRange.isOnlyNonNegative()) { // right can't be negative
						IF_CHANGE(rightRange.monoSet(ValueSet::createNonNegativeRange(be->right->getWidth())),
											be->right, hasChanged)
						IF_CHANGE(rightRange.restrictMin(leftRange.min() + 1), be->right,
											hasChanged)
					} else if (rightRange.isOnlyNegative()) { // left must be negative
						IF_CHANGE(leftRange.monoSet(ValueSet::createNegativeRange(be->left->getWidth())),
											be->left, hasChanged)
						IF_CHANGE(leftRange.restrictMax(rightRange.min() - 1), be->left,
											hasChanged)
					}
				} else if (!range.value()) { // left >= right
					if (leftRange.isOnlyNegative()) { // right must be negative
						IF_CHANGE(rightRange.monoSet(ValueSet::createNegativeRange(be->right->getWidth())),
											be->right, hasChanged)
						IF_CHANGE(rightRange.restrictMax(leftRange.max()), be->right,
											hasChanged)
					} else if (rightRange.isOnlyNonNegative()) { // left can't be negative
						IF_CHANGE(leftRange.monoSet(ValueSet::createNonNegativeRange(be->left->getWidth())),
											be->left, hasChanged)
						IF_CHANGE(leftRange.restrictMin(rightRange.min()), be->left,
											hasChanged)
					}
				}
			}

			if (be->left == be->right) // left == right, so !(left < right)
				hasChanged |= range.monoSet(ValueSet(1, false));
			else if (leftRange.unique() && rightRange.unique()) {
				if (leftRange.slt(rightRange))
					hasChanged |= range.monoSet(ValueSet(1, true));
				else
					hasChanged |= range.monoSet(ValueSet(1, false));
			} else if (leftRange.isOnlyNegative() && rightRange.isOnlyNonNegative())
				hasChanged |= range.monoSet(ValueSet(1, true));
			else if (leftRange.isOnlyNonNegative() && rightRange.isOnlyNegative())
				hasChanged |= range.monoSet(ValueSet(1, false));

			break;
		}

		case Expr::Sle: {
			BinaryExpr *be = cast<BinaryExpr>(e);

			ValueSet &leftRange = getRange(be->left);
			ValueSet &rightRange = getRange(be->right);

			hasChanged |= processExpr(be->left, e, depth+1);
			hasChanged |= processExpr(be->right, e, depth+1);

			bool update = hasChanged || leftRange.unique() || rightRange.unique() ||
											((leftRange.isFullRange() || isa<ConstantExpr>(be->left))
											 && (rightRange.isFullRange()));

			if (range.unique()) {
				if (range.value() && be->left != be->right) { // left <= right
					if (leftRange.isOnlyNonNegative()) { // right can't be negative
						IF_CHANGE(rightRange.monoSet(ValueSet::createNonNegativeRange(be->right->getWidth())),
											be->right, hasChanged)
						IF_CHANGE(rightRange.restrictMin(leftRange.min()), be->right,
											hasChanged)
					} else if (rightRange.isOnlyNegative()) { // left must be negative
						IF_CHANGE(leftRange.monoSet(ValueSet::createNegativeRange(be->left->getWidth())),
											be->left, hasChanged)
						IF_CHANGE(leftRange.restrictMax(rightRange.max()), be->left,
											hasChanged)
					}
				} else if (!range.value() && update) { // left > right
					if (leftRange.isOnlyNegative()) { // right must be negative
						IF_CHANGE(rightRange.monoSet(ValueSet::createNegativeRange(be->right->getWidth())),
											be->right, hasChanged)
						IF_CHANGE(rightRange.restrictMax(leftRange.max() - 1), be->right,
											hasChanged)
					} else if (rightRange.isOnlyNonNegative()) { // left can't be negative
						IF_CHANGE(leftRange.monoSet(ValueSet::createNonNegativeRange(be->left->getWidth())),
											be->left, hasChanged)
						IF_CHANGE(leftRange.restrictMin(rightRange.min() + 1), be->left,
											hasChanged)
					}
				}
			}

			if (be->left == be->right) // left == right, so left <= right
				hasChanged |= range.monoSet(ValueSet(1, true));
			else if (leftRange.unique() && rightRange.unique()) {
				if (leftRange.sle(rightRange))
					hasChanged |= range.monoSet(ValueSet(1, true));
				else
					hasChanged |= range.monoSet(ValueSet(1, false));
			} else if (leftRange.isOnlyNegative() && rightRange.isOnlyNonNegative())
				hasChanged |= range.monoSet(ValueSet(1, true));
			else if (leftRange.isOnlyNonNegative() && rightRange.isOnlyNegative())
				hasChanged |= range.monoSet(ValueSet(1, false));

			break;
		}

		case Expr::Shl: {
			BinaryExpr *be = cast<BinaryExpr>(e);

			ValueSet &leftRange = getRange(be->left);
			ValueSet &rightRange = getRange(be->right);

			hasChanged |= processExpr(be->left, e, depth+1);
			hasChanged |= processExpr(be->right, e, depth+1);

			if (rightRange.unique()) { // known shift amount
				hasChanged |= range.monoSet(leftRange.shl(rightRange));
			}

			// if we know the range and the shift amount, we know at least a range for
			// leftRange (source value) by shifting right
			if (range.unique() && rightRange.unique()) {
				IF_CHANGE(leftRange.monoSet(ValueSet(range.lshr(rightRange),
																						 range.ashr(rightRange))),
									be->left, hasChanged)
			}
			// if we know the range and the leftRange, we can determine a lower bound
			// for the rightRange (shift amount)
			else if (range.unique() && leftRange.unique()) {
				uint64_t val = leftRange.value(), i = 0;
				for (; val != range.value() && i <= leftRange.getWidth(); i++)
					val <<= 1;
				if (val != range.value())
					contradiction = true; // no shift amount yields the given range
				else
					IF_CHANGE(rightRange.restrictMin(i), be->right, hasChanged)

				// non-zero range means we can give an upper bound for the rightRange
				// (shift amount)
				if (range.value()) {
					uint64_t maxMatch = 0;
					for (i = 0; i < leftRange.getWidth(); i++) {
						if (leftRange.value() << i == range.value())
							maxMatch = 0;
					}
					IF_CHANGE(rightRange.restrictMax(maxMatch), be->right, hasChanged)
				}
			}
			// zero shift (we should just optimize these out)
			else if (rightRange.unique() && !rightRange.value())
				IF_CHANGE(leftRange.monoSet(range), be->left, hasChanged)
			else if (rightRange.unique() && rightRange.value() > be->left->getWidth())
				IF_CHANGE(leftRange.monoSet(ValueSet(be->left->getWidth(), 0)),
									be->left, hasChanged)

			break;
		}

		case Expr::LShr: {
			BinaryExpr *be = cast<BinaryExpr>(e);

			ValueSet &leftRange = getRange(be->left);
			ValueSet &rightRange = getRange(be->right);

			hasChanged |= processExpr(be->left, e, depth+1);
			hasChanged |= processExpr(be->right, e, depth+1);

			if (rightRange.unique()) { // known shift amount
				IF_CHANGE(leftRange.monoSet(range.shl_unknown(rightRange)), be->left,
									hasChanged)
			}

			if (leftRange.unique() && rightRange.unique())
				hasChanged |= range.monoSet(leftRange.lshr(rightRange));

			break;
		}

		case Expr::AShr: {
			BinaryExpr *be = cast<BinaryExpr>(e);

			ValueSet &leftRange = getRange(be->left);
			ValueSet &rightRange = getRange(be->right);

			hasChanged |= processExpr(be->left, e, depth+1);
			hasChanged |= processExpr(be->right, e, depth+1);

			if (rightRange.unique()) { // known shift amount
				IF_CHANGE(leftRange.monoSet(range.shl_unknown(rightRange)), be->left,
									hasChanged)
			}

			if (leftRange.unique() && rightRange.unique())
				hasChanged |= range.monoSet(leftRange.ashr(rightRange));

			break;
		}

		case Expr::Mul: {
			BinaryExpr *be = cast<BinaryExpr>(e);

			ValueSet &leftRange = getRange(be->left);
			ValueSet &rightRange = getRange(be->right);

			hasChanged |= processExpr(be->left, e, depth+1);
			hasChanged |= processExpr(be->right, e, depth+1);

			hasChanged |= range.monoSet(leftRange.mul(rightRange));

			// TODO: left = (range / right) +/- 1
			//			 right = (range / left) +/- 1

			break;
		}

		case Expr::UDiv: {
			BinaryExpr *be = cast<BinaryExpr>(e);

			ValueSet &leftRange = getRange(be->left);
			ValueSet &rightRange = getRange(be->right);

			hasChanged |= processExpr(be->left, e, depth+1);
			hasChanged |= processExpr(be->right, e, depth+1);

			if (rightRange.unique() && rightRange.value() == 0) {
				contradiction = true;
				break;
			}

			if (leftRange.unique() && rightRange.unique())
				hasChanged |= range.monoSet(leftRange.udiv(rightRange));

			break;
		}

		case Expr::SDiv: {
			BinaryExpr *be = cast<BinaryExpr>(e);

			ValueSet &leftRange = getRange(be->left);
			ValueSet &rightRange = getRange(be->right);

			hasChanged |= processExpr(be->left, e, depth+1);
			hasChanged |= processExpr(be->right, e, depth+1);

			if (rightRange.unique() && rightRange.value() == 0) {
				contradiction = true;
				break;
			}

			if (leftRange.unique() && rightRange.unique())
				hasChanged |= range.monoSet(leftRange.sdiv(rightRange));

			break;
		}

		case Expr::URem: {
			BinaryExpr *be = cast<BinaryExpr>(e);

			ValueSet &leftRange = getRange(be->left);
			ValueSet &rightRange = getRange(be->right);

			hasChanged |= processExpr(be->left, e, depth+1);
			hasChanged |= processExpr(be->right, e, depth+1);

			hasChanged |= range.restrictMax(rightRange.max() - 1);

			if (rightRange.unique() && rightRange.value() == 0) {
				contradiction = true;
				break;
			}

			if (leftRange.unique() && rightRange.unique())
				hasChanged |= range.monoSet(leftRange.urem(rightRange));

			break;
		}

		case Expr::SRem: {
			BinaryExpr *be = cast<BinaryExpr>(e);

			ValueSet &leftRange = getRange(be->left);
			ValueSet &rightRange = getRange(be->right);

			hasChanged |= processExpr(be->left, e, depth+1);
			hasChanged |= processExpr(be->right, e, depth+1);

			if (rightRange.unique() && rightRange.value() == 0) {
				contradiction = true;
				break;
			}

			if (leftRange.unique() && rightRange.unique())
				hasChanged |= range.monoSet(leftRange.srem(rightRange));

			break;
		}

		case Expr::Ne:
		case Expr::Ugt:
		case Expr::Uge:
		case Expr::Sgt:
		case Expr::Sge:
			assert(0 && "invalid expressions (uncanonicalized)");
			break;

		default:
#ifdef FRS_DEBUG
			e->dump();
#endif
			assert(0 && "invalid type");
			break;
		}

		hasEverChanged |= hasChanged;

		if (range.empty()) {
			contradiction = true;
			break;
		}

		if (util::estWallTime() - startTime > FastRangeTimeout) {
			timeout = true;
			return false;
		}

	} // while(hasChanged)

	if (contradiction || unsupported || timeout)
		return false;
	else
		return hasEverChanged;
}

bool FastRangeSolver::computeSat(const Query& q)
{
	RangeSimplifier	rs;
	bool		hasSolution, ok;

	ok = rs.run(q.negateExpr(), hasSolution);
	if (!ok) {
		failQuery();
		return false;
	}

	/* run(query, hasSolution); isValid = !hasSolution */
	return hasSolution;
}

ref<Expr> FastRangeSolver::computeValue(const Query& query)
{
	ref<Expr>	ret;
	bool		hasSolution;
	Assignment	a(query.expr);

	// Find the object used in the expression, and compute an assignment
	// for them.
	hasSolution = computeInitialValues(query.withFalse(), a);
	if (failed()) return ret;

	assert (hasSolution && "state has invalid constraint set");

	// Evaluate the expression with the computed assignment.
	ret = a.evaluate(query.expr);
	return ret;
}

bool FastRangeSolver::computeInitialValues(const Query& query, Assignment& a)
{
	RangeSimplifier	rs;
	bool		hasSolution, satisfies, guess, success;

	numQueries++;

	success = rs.run(query, hasSolution);
	if (!success) {
		failQuery();
		numUnsupported++;
		return false;
	}

	if (!hasSolution) {
#ifdef FRS_DEBUG
		std::cerr << "\nFastRangeSolver VALID\n";
		query.print(std::cerr);
#endif
		numSuccess++;
		return false;	/* valid => no cex exists */
	}

	assert (hasSolution && success);
#ifdef FRS_DEBUG
	std::cerr << "\nFastRangeSolver INVALID:\n";
	query.print(std::cerr);
	std::cerr << "OBJSIZE = " << a.getNumFree() << "\n";
#endif

#ifdef FRS_DEBUG
	for (unsigned i = 0; i < objects.size(); i++)
		rs.printCex(objects[i], std::vector<unsigned char>());
#endif

	guess = !rs.findCex(query, a);
#ifdef FRS_DEBUG
	std::cerr << "GUESS ON FINDCEX: " << guess << std::endl;
#endif
	if (a.getNumBindings() == 0) {
		numSuccess++;
		return true;
	}

	success &= (a.getNumFree() == 0);
#ifdef FRS_DEBUG

	foreach (it, a.bindingsBegin(), a.bindingsEnd())
		rs.printCex(*it, a.getBinding(*it));
	std::cerr << "SUCCESS = " << success << std::endl;
	std::cerr << a.getNumBindings() << " = size\n";
#endif
	satisfies = false;
	if (success) {
		std::set<ref<Expr> >	exprs(	query.constraints.begin(),
						query.constraints.end());

		exprs.insert(Expr::createIsZero(query.expr));
		satisfies = a.satisfies(exprs.begin(), exprs.end());
		// XXX check for Expr::CreateIsZero being a constant false
	}

	if (!satisfies) {
#ifdef FRS_DEBUG
		std::cerr << "Satisfying assignment failed!\n";
		ExprPPrinter::printQuery(std::cerr, query.constraints, query.expr);
#endif
		if (!guess && success) {
			ExprPPrinter::printQuery(
				std::cerr, query.constraints, query.expr);
			assert(0 && "Known satisfying assignment failed");
		}

		a.resetBindings();
		failQuery();
		return false;
	}

	numSuccess++;
	return true;
}

Solver *klee::createFastRangeSolver(Solver* complete_solver)
{
	return new Solver(
		new StagedSolverImpl(
			new Solver(new FastRangeSolver()),
			complete_solver));
}

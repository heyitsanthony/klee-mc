#ifndef INDEPENDENT_ELEMENT_SET_H
#define INDEPENDENT_ELEMENT_SET_H

#include "static/Sugar.h"
#include "klee/util/ExprUtil.h"

namespace klee
{

class Query;

class ReadSet : public std::set<ref<ReadExpr> >
{
public:
	friend class ref<ReadSet>;
	virtual ~ReadSet() {}
	static ref<ReadSet> get(ref<Expr>& e)
	{
		ref<ReadSet> rs(new ReadSet());
		std::vector< ref<ReadExpr> > reads;
		ExprUtil::findReads(e, /* visitUpdates= */ true, reads);
		for (auto r : reads)
			rs->insert(r);
		return rs;
	}
protected:
	ReadSet() : refCount(0) {}
protected:
	unsigned refCount;
};

template<class T>
class DenseSet
{
	typedef std::set<T> set_ty;
	set_ty s;
public:
	DenseSet() {}

	void add(T x) { s.insert(x); }
	void add(T start, T end)
	{ for (; start<end; start++) s.insert(start); }

	bool add(const DenseSet &b) {
		bool modified = false;
		for (auto& b_s : b.s) {
			if (modified || !s.count(b_s)) {
				modified = true;
				s.insert(b_s);
			}
		}
		return modified;
	}

	bool intersects(const DenseSet &b) {
		foreach (it, s.begin(), s.end()) {
			if (b.s.count(*it))
				return true;
		}
		return false;
	}

	void print(std::ostream &os) const {
		bool first = true;
		os << "{";
		foreach (it, s.begin(), s.end()) {
			if (first) {
				first = false;
			} else {
				os << ",";
			}
			os << *it;
		}
		os << "}";
	}
};

template<class T>
inline std::ostream &operator<<(std::ostream &os, const DenseSet<T> &dis)
{ dis.print(os); return os; }

class IndependentElementSet
{
public:
	IndependentElementSet() {}
	IndependentElementSet(ref<Expr> e);
	IndependentElementSet(ref<Expr> e, ref<ReadSet> rs);

	IndependentElementSet(const IndependentElementSet &ies)
	: elements(ies.elements)
	, wholeObjects(ies.wholeObjects) {}

	IndependentElementSet &operator=(const IndependentElementSet &ies)
	{
		elements = ies.elements;
		wholeObjects = ies.wholeObjects;
		return *this;
	}

	void print(std::ostream &os) const;
	bool intersects(const IndependentElementSet &b); 

	// returns true iff set is changed by addition
	bool add(const IndependentElementSet &b);

	static IndependentElementSet getIndependentConstraints(
		const Query& query,
		std::vector< ref<Expr> >& result);

private:
	void addRead(ref<ReadExpr>& re);

	typedef std::map<const Array*, DenseSet<unsigned> > elements_ty;
	elements_ty			elements;
	std::set<const Array*>		wholeObjects;
};

}
#endif

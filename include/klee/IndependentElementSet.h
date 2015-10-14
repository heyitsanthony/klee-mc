#ifndef INDEPENDENT_ELEMENT_SET_H
#define INDEPENDENT_ELEMENT_SET_H

#include "static/Sugar.h"
#include "klee/util/ExprUtil.h"
#include <unordered_set>
#include <unordered_map>

namespace std {
template <> struct hash<klee::ref<klee::ReadExpr>>
{
size_t operator()(const klee::ref<klee::ReadExpr>& x) const { return x->hash(); }
};
}

namespace klee
{

class ConstraintManager;
class Query;

class ReadSet : public std::unordered_set<ref<ReadExpr> >
{
public:
	friend class ref<ReadSet>;
	virtual ~ReadSet() {}
	static ref<ReadSet> get(ref<Expr>& e)
	{
		ref<ReadSet> rs(new ReadSet());
		std::vector< ref<ReadExpr> > reads;
		ExprUtil::findReads(e, /* visitUpdates= */ true, reads);
		for (auto r : reads) rs->insert(r);
		return rs;
	}

	int compare(const ReadSet& rs) const
	{
		if (rs.size() != size()) return rs.size() - size();
		for (const auto& re : rs) {
			if (!count(re))
				return 1;
		}
		return 0;
	}

	void print(std::ostream& os) const
	{ for (const auto &re : *this) os << re << '\n'; }
protected:
	ReadSet() : refCount(0) {}
protected:
	unsigned refCount;
};

template<class T>
class DenseSet
{
	typedef std::unordered_set<T> set_ty;
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

	bool intersects(const DenseSet &b) const {
		for (auto& s_e : s) {
			if (b.s.count(s_e))
				return true;
		}
		return false;
	}

	void print(std::ostream &os) const {
		bool first = true;
		os << "{";
		for (auto& s_e : s) {
			if (first) {
				first = false;
			} else {
				os << ",";
			}
			os << s_e;
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
	bool intersects(const IndependentElementSet &b) const; 

	// returns true iff set is changed by addition
	bool add(const IndependentElementSet &b);

	static IndependentElementSet getIndependentConstraints(
		const Query& query,
		ConstraintManager& cs);

private:
	void addRead(ref<ReadExpr>& re);
	bool elem_intersect(const IndependentElementSet& b) const;
	bool obj_intersect(const IndependentElementSet& b) const;

	typedef std::unordered_map<const Array*, DenseSet<unsigned> >
		elements_ty;
	elements_ty				elements;
	std::unordered_set<const Array*>	wholeObjects;
};

}
#endif

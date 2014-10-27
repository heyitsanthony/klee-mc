#include "klee/Expr.h"
#include "klee/IndependentElementSet.h"
#include "klee/Constraints.h"
#include "klee/Query.h"

using namespace klee;

void IndependentElementSet::print(std::ostream& os) const
{
	os << "{";
	bool first = true;
	foreach (it, wholeObjects.begin(), wholeObjects.end()) {
		const Array *array = *it;

		if (first) {
			first = false;
		} else {
			os << ", ";
		}

		os << "MO" << array->name;
	}

	foreach (it, elements.begin(), elements.end()) {
		const Array *array = it->first;
		const DenseSet<unsigned> &dis = it->second;

		if (first) {
		first = false;
		} else {
		os << ", ";
		}

		os << "MO" << array->name << " : " << dis;
	}
	os << "}";
}

// more efficient when this is the smaller set
bool IndependentElementSet::intersects(const IndependentElementSet &b)
{
	foreach (it, wholeObjects.begin(), wholeObjects.end()) {
		const Array *array = *it;
		if (	b.wholeObjects.count(array) ||
			b.elements.find(array) != b.elements.end())
			return true;
	}

	foreach (it, elements.begin(), elements.end()) {
		const Array *array = it->first;
		elements_ty::const_iterator it2;

		if (b.wholeObjects.count(array))
			return true;
		it2 = b.elements.find(array);
		if (it2 != b.elements.end()) {
			if (it->second.intersects(it2->second))
				return true;
		}
	}

	return false;
}

bool IndependentElementSet::add(const IndependentElementSet &b)
{
	bool	modified = false;

	/* add whole objects */
	foreach (it, b.wholeObjects.begin(), b.wholeObjects.end()) {
		const Array *array;
		elements_ty::iterator it2;

		/* if a partial object in this, update to whole object now */
		it2 = elements.find(array);
		if (it2 != elements.end()) {
			modified = true;
			elements.erase(it2);
			wholeObjects.insert(array);
			continue;
		}

		/* not a partial object, add to whole object */
		array = *it;
		if (!wholeObjects.count(array)) {
			modified = true;
			wholeObjects.insert(array);
		}
	}

	/* add partial objects */
	foreach (it, b.elements.begin(), b.elements.end()) {
		const Array *array = it->first;
		elements_ty::iterator it2;

		if (wholeObjects.count(array))
			continue;

		it2 = elements.find(array);
		if (it2==elements.end()) {
			modified = true;
			elements.insert(*it);
			continue;
		}

		if (it2->second.add(it->second))
			modified = true;
	}

	return modified;
}

IndependentElementSet::IndependentElementSet(ref<Expr> e)
{
	std::vector< ref<ReadExpr> > reads;
	ExprUtil::findReads(e, /* visitUpdates= */ true, reads);
	for (unsigned i = 0; i != reads.size(); ++i) {
		addRead(reads[i]);
	}
}

IndependentElementSet::IndependentElementSet(ref<Expr> e, ref<ReadSet> rs)
{
	for (auto r : *rs)
		addRead(r);
}


void IndependentElementSet::addRead(ref<ReadExpr>& re)
{
	const Array *array = re->updates.getRoot().get();

	// Reads of a constant array don't alias.
	if (	re->updates.getRoot().get()->isConstantArray() &&
		re->updates.head == NULL)
	{
		return;
	}

	if (wholeObjects.count(array))
		return;

	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(re->index)) {
		DenseSet<unsigned> &dis = elements[array];
		dis.add((unsigned) CE->getZExtValue(32));
	} else {
		elements_ty::iterator it2 = elements.find(array);
		if (it2!=elements.end())
			elements.erase(it2);
		wholeObjects.insert(array);
	}
}


inline std::ostream &operator<<(
	std::ostream &os, const IndependentElementSet &ies)
{ ies.print(os); return os; }


typedef std::vector<
	std::pair<ref<Expr>, IndependentElementSet> > worklist_ty;

IndependentElementSet IndependentElementSet::getIndependentConstraints(
	const Query& query,
	ConstraintManager&  cs)
{
	IndependentElementSet	eltsClosure(query.expr);
	worklist_ty		worklist;
	std::vector<bool>	worklistDone; // because all the copying is expensive
	std::vector<int>	result;

	for (unsigned i = 0; i < query.constraints.size(); i++) {
		ref<Expr> e(query.constraints.getConstraint(i));
		worklist.push_back(
			std::make_pair(
				e,
				IndependentElementSet(
					e, query.constraints.getReadset(i))));
		worklistDone.push_back(false);
	}

	// Copies here were really inefficient. Still kind of pricey.
	bool done = false;
	while (done == false) {
		worklist_ty	newWorklist;

		done = true;
		for (unsigned i = 0; i < worklist.size(); i++) {
			if (worklistDone[i])
				continue;

			// evaluate in next work set
			if (!worklist[i].second.intersects(eltsClosure))
				continue;

			// not considered for next work set
			worklistDone[i] = true;
			done = !(eltsClosure.add(worklist[i].second));
			result.push_back(i);
		}
	}

	ConstraintManager::constraints_t	constrs;
	ConstraintManager::readsets_t		rs;
	for (unsigned i = 0; i < result.size(); i++) {
		constrs.push_back(query.constraints.getConstraint(result[i]));
		rs.push_back(query.constraints.getReadset(result[i]));
	}
	cs = ConstraintManager(constrs, rs);

	return eltsClosure;
}
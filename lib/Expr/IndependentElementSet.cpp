#include "klee/Expr.h"
#include "klee/IndependentElementSet.h"
#include "klee/Constraints.h"
#include "klee/Query.h"

using namespace klee;

void IndependentElementSet::print(std::ostream& os) const
{
	os << "{";
	bool first = true;
	for (auto array : wholeObjects) {
		if (first) {
			first = false;
		} else {
			os << ", ";
		}
		os << "MO " << array->name;
	}

	for (const auto &p : elements) {
		const Array *array = p.first;
		const DenseSet<unsigned> &dis = p.second;
		if (first) {
			first = false;
		} else {
			os << ", ";
		}
		os << "MO " << array->name << " : " << dis;
	}
	os << "}";
}

bool IndependentElementSet::elem_intersect(const IndependentElementSet& b) const
{
	for (const auto &p : elements) {
		const Array *array = p.first;

		if (b.wholeObjects.count(array))
			return true;

		auto it2 = b.elements.find(array);
		if (it2 != b.elements.end()) {
			if (p.second.intersects(it2->second))
				return true;
		}
	}
	return false;
}

bool IndependentElementSet::obj_intersect(const IndependentElementSet& b) const
{
	for (auto arr : wholeObjects) {
		if (b.wholeObjects.count(arr) || b.elements.count(arr))
			return true;
	}
	return false;
}

bool IndependentElementSet::intersects(const IndependentElementSet &b) const
{ return obj_intersect(b) || elem_intersect(b); }

bool IndependentElementSet::add(const IndependentElementSet &b)
{
	bool	modified = false;

	/* add whole objects */
	for (auto array : b.wholeObjects) {
		/* if a partial object in this, update to whole object now */
		if (elements.erase(array)) {
			modified = true;
			wholeObjects.insert(array);
		} else if (wholeObjects.insert(array).second) {
			 /* not a partial object, add to whole object */
			modified = true;
		}
	}

	/* add partial objects */
	for (const auto &p : b.elements) {
		const Array *array = p.first;

		if (wholeObjects.count(array))
			continue;

		auto it2 = elements.find(array);
		if (it2 == elements.end()) {
			modified = true;
			elements.insert(p);
			continue;
		}

		modified |= it2->second.add(p.second);
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
{ for (auto r : *rs) addRead(r); }


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
		// concrete index => continue precise read set
		DenseSet<unsigned> &dis(elements[array]);
		dis.add((unsigned) CE->getZExtValue(32));
	} else {
		// symbolic index => all of array may bepossible
		elements.erase(array);
		wholeObjects.insert(array);
	}
}


inline std::ostream &operator<<(
	std::ostream &os, const IndependentElementSet &ies)
{ ies.print(os); return os; }


typedef std::vector<IndependentElementSet*> worklist_ty;

IndependentElementSet IndependentElementSet::getIndependentConstraints(
	const Query& query,
	ConstraintManager&  cs)
{
	IndependentElementSet	eltsClosure(query.expr);
	worklist_ty		worklist;
	std::vector<bool>	worklistDone; // because all the copying is expensive
	std::vector<int>	result; // indexes of dependent constraints

	// build an independent element set for each constraint
	for (unsigned i = 0; i < query.constraints.size(); i++) {
		ref<Expr>	e(query.constraints.getConstraint(i));
		ref<ReadSet>	rs(query.constraints.getReadset(i));

		worklist.push_back(new IndependentElementSet(e, rs));
		worklistDone.push_back(false);
	}

	// Copies here were really inefficient. Still kind of pricey.
	bool done = false;
	while (done == false) {
		// set to false if anything added in this round
		done = true;

		/* does constraint readset intersect with the completion
		 * for the q.expr completion? */
		for (unsigned i = 0; i < worklist.size(); i++) {
			bool added_more;
			if (worklistDone[i])
				continue;

			// evaluate in next work set; may pick up more elements
			if (!worklist[i]->intersects(eltsClosure)) {
				continue;
			}

			// not considered for next work set
			worklistDone[i] = true;
			result.push_back(i);

			// if the closure set changed, will need to reprocess
			added_more = eltsClosure.add(*worklist[i]);
			done = done && !added_more;
		}
	}
	for (auto& wl : worklist ) delete wl;

	/* store q.expr's dependent constraints to a constraint manager */
	ConstraintManager::constraints_t	constrs;
	ConstraintManager::readsets_t		rs;
	for (unsigned i = 0; i < result.size(); i++) {
		constrs.push_back(query.constraints.getConstraint(result[i]));
		rs.push_back(query.constraints.getReadset(result[i]));
	}
	cs = ConstraintManager(constrs, rs);

	return eltsClosure;
}
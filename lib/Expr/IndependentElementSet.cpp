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

		os << "MO " << array->name;
	}

	foreach (it, elements.begin(), elements.end()) {
		const Array *array = it->first;
		const DenseSet<unsigned> &dis = it->second;

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

bool IndependentElementSet::obj_intersect(const IndependentElementSet& b) const
{
	foreach (it, wholeObjects.begin(), wholeObjects.end()) {
		if (b.wholeObjects.count(*it) || b.elements.count(*it))
			return true;
	}

	return false;
}

bool IndependentElementSet::intersects(const IndependentElementSet &b) const
{ return obj_intersect(b) ? true : elem_intersect(b); }

bool IndependentElementSet::add(const IndependentElementSet &b)
{
	bool	modified = false;

	/* add whole objects */
	foreach (it, b.wholeObjects.begin(), b.wholeObjects.end()) {
		const Array *array(*it);

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
		done = true;

		/* does constraint readset intersect with the completion
		 * for the q.expr completion? */
		for (unsigned i = 0; i < worklist.size(); i++) {
			bool added_more;
			if (worklistDone[i])
				continue;

			// evaluate in next work set
			if (!worklist[i]->intersects(eltsClosure)) {
				continue;
			}

			// not considered for next work set
			worklistDone[i] = true;
			result.push_back(i);

			// if the closure set changed, will need to reprocess
			added_more = !(eltsClosure.add(*worklist[i]));
		#if 0
			if (!added_more) {
				std::cerr << "couldn't amend with: ";
				worklist[i]->print(std::cerr);
				std::cerr << '\n';
			} else {
				std::cerr << "amend with: ";
				worklist[i]->print(std::cerr);
				std::cerr << '\n';
			}
		#endif
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
//===-- Assignment.h --------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_UTIL_ASSIGNMENT_H
#define KLEE_UTIL_ASSIGNMENT_H

#include <map>
#include "klee/util/ExprEvaluator.h"

namespace klee
{
class Array;
class AssignmentEvaluator;

class Assignment {
public:
	typedef std::map<
		const Array*,
		std::vector<unsigned char>,
		Array::Compare> bindings_ty;

	bindings_ty bindings;

public:
	bool allowFreeValues;

	Assignment(bool _allowFreeValues=false)
	: allowFreeValues(_allowFreeValues)
	{}

	Assignment(const Assignment& a)
	{
		bindings = a.bindings;
		allowFreeValues = a.allowFreeValues;
	}

	Assignment(
		const std::vector<const Array*> &objects,
		std::vector< std::vector<unsigned char> > &values,
		bool _allowFreeValues=false);

	virtual ~Assignment(void) {}

	ref<Expr> evaluate(const Array *mo, unsigned index) const;
	ref<Expr> evaluate(ref<Expr> e);
	ref<Expr> evaluate(ref<Expr> e, bool& wasDivProtected);

	template<typename InputIterator>
	bool satisfies(InputIterator begin, InputIterator end);
	bool satisfies(ref<Expr> e) { return evaluate(e)->isTrue(); }

	void save(const char* path) const;
	bool load(
		const std::vector<const Array*>& objs,
		const char* path);

	const std::vector<unsigned char>* getBinding(const Array* a)
	const
	{
		bindings_ty::const_iterator	it(bindings.find(a));
		return ((it == bindings.end()) ? NULL : &it->second);
	}

private:
	void addBinding(
		const Array* a,
		const std::vector<unsigned char>& v)
	{ bindings.insert(std::make_pair(a, v)); }
};

class AssignmentEvaluator : public ExprEvaluator
{
	const Assignment &a;
public:
	virtual ref<Expr> getInitialValue(const Array &mo, unsigned index)
	{ return a.evaluate(&mo, index); }
	AssignmentEvaluator(const Assignment &_a) : a(_a)
	{ }
	virtual ~AssignmentEvaluator() {}
};

/* FIXME: is it worth it to inline this? doubtful. */

/* concretize array read with assignment value */
inline ref<Expr> Assignment::evaluate(
	const Array *array, unsigned index) const
{
	bindings_ty::const_iterator it = bindings.find(array);

	/* found, can immediately resolve */
	if (it != bindings.end() && index < it->second.size()) {
		return ConstantExpr::alloc(it->second[index], Expr::Int8);
	}

	/* keep the free value (e.g. dump all unassigned arrays) */
	if (allowFreeValues) {
		return ReadExpr::create(
			UpdateList(array, 0),
			ConstantExpr::alloc(index, Expr::Int32));
	}

	/* default, 0 value */
	return ConstantExpr::alloc(0, Expr::Int8);
}

inline ref<Expr> Assignment::evaluate(ref<Expr> e)
{
	AssignmentEvaluator v(*this);
	return v.visit(e);
}

inline ref<Expr> Assignment::evaluate(ref<Expr> e, bool &wasZeroDiv)
{
	AssignmentEvaluator	v(*this);
	ref<Expr>		ret;

	ret = v.visit(e);
	wasZeroDiv = v.wasDivProtected();

	return ret;
}

template<typename InputIterator>
inline bool Assignment::satisfies(InputIterator begin, InputIterator end)
{
	AssignmentEvaluator v(*this);
	for (; begin!=end; ++begin)
		if (!v.visit(*begin)->isTrue())
			return false;
	return true;
}

}
#endif

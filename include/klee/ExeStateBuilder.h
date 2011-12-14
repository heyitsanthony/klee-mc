#ifndef EXESTATEBUILDER_H
#define EXESTATEBUILDER_H

#include "klee/Expr.h"

namespace klee
{

class ExecutionState;
class KFunction;

class ExeStateBuilder
{
public:
	static ExecutionState* create(void)
	{ return theESB->_create(); }

	static ExecutionState* create(KFunction* kf)
	{ return theESB->_create(kf); }

	static ExecutionState* create(const std::vector<ref<Expr> >& assumptions)
	{ return theESB->_create(assumptions); }

	static ExeStateBuilder* setBuilder(ExeStateBuilder* esb)
	{
		ExeStateBuilder* old_builder = theESB;
		theESB = esb;
		return old_builder;
	}

	static void replaceBuilder(ExeStateBuilder* esb)
	{
		if (theESB != NULL)
			delete theESB;
		theESB = esb;
	}

	virtual ~ExeStateBuilder() {}

protected:
	virtual ExecutionState* _create(void) const = 0;
	virtual ExecutionState* _create(KFunction* kf) const = 0;
	virtual ExecutionState* _create(const std::vector<ref<Expr> >& assumptions)
		const = 0;

private:
	static ExeStateBuilder	*theESB;
};

template <class T>
class DefaultExeStateBuilder : public ExeStateBuilder
{
public:
	DefaultExeStateBuilder () {}
	virtual ~DefaultExeStateBuilder() {}

protected:
	virtual T* _create(void) const
	{ return new T(); }
	virtual T* _create(KFunction* kf) const
	{ return new T(kf); }
	virtual T* _create(const std::vector<ref<Expr> >& assumptions) const
	{ return new T(assumptions); }
};

}

#endif

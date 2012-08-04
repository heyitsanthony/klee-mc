#ifndef SHADOWEXECUTOR_H
#define SHADOWEXECUTOR_H

#include "klee/Common.h"
#include "ExeStateManager.h"
#include "ObjectState.h"
#include "ShadowCore.h"

namespace klee
{

class ExecutionState;
class InterpreterHandler;

template<typename T>
class ShadowExecutor : public T
{
public:
	ShadowExecutor(InterpreterHandler* ie)
	: T(ie), shadowCore(this) {}
	virtual ~ShadowExecutor() {}

	virtual void terminateState(ExecutionState &state)
	{
		// never reached searcher, just delete immediately
		T::terminateState(state);
	}

	virtual bool addConstraint(ExecutionState &state, ref<Expr> condition)
	{
		shadowCore.addConstraint(state, condition);
		return T::addConstraint(state, condition);
	}

	virtual void run(ExecutionState &initialState)
	{
		shadowCore.setupInitialState(&initialState);
		T::run(initialState);
	}


private:
	ShadowCore	shadowCore;
};
};

#endif

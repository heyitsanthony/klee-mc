#ifndef SHADOWEXECUTOR_H
#define SHADOWEXECUTOR_H

#include "klee/Common.h"
#include "ExeStateManager.h"
#include "ObjectState.h"
#include "ShadowCore.h"
#include "ShadowMMU.h"

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

	virtual void terminate(ExecutionState &state)
	{
		// never reached searcher, just delete immediately
		T::terminate(state);
	}

	virtual bool addConstraint(ExecutionState &state, ref<Expr> condition)
	{
		shadowCore.addConstraint(state, condition);
		return T::addConstraint(state, condition);
	}

	virtual void run(ExecutionState &initialState)
	{
		shadowCore.setupInitialState(&initialState);

		if (MMU::isSymMMU() == false) {
			if (T::mmu == NULL) T::mmu = MMU::create(*this);
			T::mmu = new ShadowMMU(T::mmu);
		}
		T::run(initialState);
	}


private:
	ShadowCore	shadowCore;
};
};

#endif

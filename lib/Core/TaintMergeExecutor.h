#ifndef TAINTMERGEEXECUTOR_H
#define TAINTMERGEEXECUTOR_H

#include "klee/Common.h"
#include "ExeStateManager.h"
#include "TaintMergeCore.h"

namespace klee
{

class ExecutionState;
class InterpreterHandler;

template<typename T>
class TaintMergeExecutor : public T
{
public:
	TaintMergeExecutor(InterpreterHandler* ie)
	: T(ie), tmCore(this) {}
	virtual ~TaintMergeExecutor() {}

	virtual void run(ExecutionState &initialState)
	{
		tmCore.setupInitialState(&initialState);
		T::run(initialState);
	}

private:
	TaintMergeCore	tmCore;
};
};

#endif

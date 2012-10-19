#ifndef PARTSEEDEXECUTOR_H
#define PARTSEEDEXECUTOR_H

#include "klee/Common.h"
#include "../Core/ExeStateManager.h"
#include "../Core/ObjectState.h"
#include "PartSeedCore.h"
#include "SeedStateSolver.h"

namespace klee
{

class ExecutionState;
class InterpreterHandler;

template<typename T>
class PartSeedExecutor : public T
{
public:
	PartSeedExecutor(InterpreterHandler* ie)
	: T(ie), psCore(*this) {}
	virtual ~PartSeedExecutor() {}

	virtual void terminate(ExecutionState &state)
	{
		// never reached searcher, just delete immediately
		psCore.terminate(state);
		T::terminate(state);
	}

	virtual void run(ExecutionState &initialState)
	{
		psCore.setupInitialState(&initialState);
		T::run(initialState);
	}

protected:
	virtual StateSolver* createSolverChain(
		double timeout,
		const std::string& qPath,
		const std::string& logPath)
	{
		Solver		*s;
		TimedSolver	*timedSolver = NULL;
		StateSolver	*ts;

		if (timeout == 0.0)
			timeout = MaxSTPTime;

		s = Solver::createChainWithTimedSolver(qPath, logPath, timedSolver);
		ts = new SeedStateSolver(s, timedSolver);
		timedSolver->setTimeout(timeout);

		return ts;
	}

private:
	PartSeedCore	psCore;
};
};

#endif

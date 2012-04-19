#ifndef GDBEXE_H
#define GDBEXE_H

#include "GDBCore.h"

namespace klee
{

template<typename T>
class GDBExecutor : public T
{
public:
	GDBExecutor(InterpreterHandler* ie)
	: T(ie)
	, gc(this)
	{}
	virtual ~GDBExecutor() {}

	virtual void run(ExecutionState &initialState)
	{
		T::currentState = &initialState;
		processCommands();
		T::run(initialState);
	}

	virtual void stepStateInst(ExecutionState* &state)
	{
		T::stepStateInst(state);
		if (gc.isSingleStep()) {
			gc.setStopped();
			gc.writePkt("S05");
			processCommands();
		}
	}

private:
	void processCommands(void)
	{
		GDBCmd*	gcmd;

		/* begins stopped; wait for continue command */
		while (gc.isStopped()) {
			gcmd = gc.waitNextCmd();
			if (!gcmd) continue;
			delete gcmd;
		}
	}

	GDBCore	gc;
};

}
#endif

#ifndef GDBEXE_H
#define GDBEXE_H

#include "klee/ExecutionState.h"
#include "../Searcher/UserSearcher.h"
#include "GDBCore.h"
#include "../Core/CoreStats.h"

namespace klee
{

template<typename T>
class GDBExecutor : public T
{
public:
	GDBExecutor(InterpreterHandler* ie)
	: T(ie)
	, gc(this)
	, last_pc(0)
	{
		UserSearcher::setOverride();
	}

	virtual ~GDBExecutor() {}

	virtual void run(ExecutionState &initialState)
	{
		T::currentState = &initialState;
		processCommands();
		T::run(initialState);
	}

	virtual void stepStateInst(ExecutionState* &state)
	{
		uint64_t	last_fork_c;

		last_fork_c = stats::forks;

		T::stepStateInst(state);

		if (gc.watchForkBranch() && stats::forks > last_fork_c) {
			gc.handleForkBranch();
		}

		if (gc.isCont())
			return;

		if (gc.isSingleStep()) {
			uint64_t	cur_pc;

			if (last_pc == 0) {
				last_pc = state->getAddrPC();
				return;
			}

			cur_pc = state->getAddrPC();
			if (last_pc == cur_pc)
				return;

			last_pc = cur_pc;
			gc.setStopped();
			gc.writePkt("S13");
		}

		processCommands();
	}

protected:
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

	GDBCore		gc;
	uint64_t	last_pc;
};

}
#endif

#ifndef SYSCALLSSFH_H
#define SYSCALLSSFH_H

#include "../../lib/Core/SpecialFunctionHandler.h"

namespace klee
{
class ExecutorVex;
class Executor;

class SyscallSFH : public SpecialFunctionHandler
{
public:
	friend class SpecialFunctionHandler;
	typedef void (SyscallSFH::*Handler)(
		ExecutionState &state,
                KInstruction *target,
                std::vector<ref<Expr> > &arguments);

	SyscallSFH(Executor* e);
	virtual ~SyscallSFH() {}
	virtual void prepare(void);
	virtual void bind(void);

	/* for use by handlers */
	void makeRangeSymbolic(
		ExecutionState& state, void* addr, unsigned sz,
		const char* name = NULL);
private:
	void makeSymbolicTail(
		ExecutionState& state,
		const MemoryObject* mo,
		unsigned taken,
		const char* name);
	void makeSymbolicHead(
		ExecutionState& state,
		const MemoryObject* mo,
		unsigned taken,
		const char* name);
	void makeSymbolicMiddle(
		ExecutionState& state,
		const MemoryObject* mo,
		unsigned mo_off,
		unsigned taken,
		const char* name);

	ExecutorVex	*exe_vex;	
};

SFH_HANDLER(SCRegs)
SFH_HANDLER(SCBad)
SFH_HANDLER(FreeRun)
SFH_HANDLER(KMCExit)
SFH_HANDLER(MakeRangeSymbolic)
SFH_HANDLER(AllocAligned)
SFH_HANDLER(LogSysCall)
}
#endif

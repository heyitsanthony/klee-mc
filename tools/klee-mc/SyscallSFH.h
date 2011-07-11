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
private:
	ExecutorVex	*exe_vex;	
};

SFH_HANDLER(SCRegs)
SFH_HANDLER(SCBad)
SFH_HANDLER(FreeRun)
SFH_HANDLER(KMCExit)
SFH_HANDLER(MakeRangeSymbolic)
SFH_HANDLER(AllocAligned)
}
#endif

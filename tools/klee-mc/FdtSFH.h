#ifndef FDTSFH_H
#define FDTSFH_H

#include "SyscallSFH.h"

namespace klee
{
class FdtSFH : public SyscallSFH
{
public:
	friend class SpecialFunctionHandler;
	typedef void (SyscallSFH::*Handler)(
		ExecutionState &state,
                KInstruction *target,
                std::vector<ref<Expr> > &arguments);

	FdtSFH(Executor* e);
	virtual ~FdtSFH() {}
	virtual void prepare(void);
	virtual void bind(void);
};
}

SFH_HANDLER(SCGetCwd)
SFH_HANDLER(SCConcreteFileSize)
SFH_HANDLER(SCConcreteFileSnapshot)
// boo, why can't libcxx have a non-threaded define!
SFH_HANDLER(DummyThread)


#endif

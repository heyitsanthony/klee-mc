#ifndef SYSCALLSSFH_H
#define SYSCALLSSFH_H

#include "../../lib/Core/SpecialFunctionHandler.h"
#include "VFD.h"

#include <map>

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

	VFD		vfds;
private:
	void removeTail(
		ExecutionState& state,
		const MemoryObject* mo,
		unsigned taken);
	void removeHead(
		ExecutionState& state,
		const MemoryObject* mo,
		unsigned taken);
	void removeMiddle(
		ExecutionState& state,
		const MemoryObject* mo,
		unsigned mo_off,
		unsigned taken);

	ExecutorVex	*exe_vex;
};
}
#endif

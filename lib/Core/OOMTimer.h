#ifndef KLEE_OOMTIMER_H
#define KLEE_OOMTIMER_H

#include "Executor.h"

namespace klee
{
class OOMTimer : public Executor::Timer
{
public:
	OOMTimer(Executor &exe_)
		: exe(exe_)
		, lastMemoryLimitOperationInstructions(0)
	{}
	virtual ~OOMTimer() = default;

	void run() override;

	static unsigned getMaxMemory(void);
	static bool isAtMemoryLimit(void) { return atMemoryLimit; }

private:
	void killStates(void);
	void handleMemoryPID(void);

	Executor	&exe;
	/// Remembers the instruction count at the last memory limit operation.
	uint64_t lastMemoryLimitOperationInstructions;

	static bool	atMemoryLimit;
};

}
#endif

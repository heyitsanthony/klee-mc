#ifndef SYSCALLPRIORITIZER_H
#define SYSCALLPRIORITIZER_H

namespace klee
{
class SyscallPrioritizer : public Prioritizer
{
public:
	SyscallPrioritizer() {}
	virtual ~SyscallPrioritizer() {}

	int getPriority(ExecutionState& st)
	{ return -static_cast<ExeStateVex&>(st).getSyscallCount(); }
};
}
#endif

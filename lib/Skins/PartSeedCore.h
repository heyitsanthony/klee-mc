#ifndef PARTSEEDCORE_H
#define PARTSEEDCORE_H

namespace klee
{

class Executor;
class ExecutionState;

class PartSeedCore
{
public:
	PartSeedCore(Executor& _exe);
	~PartSeedCore() {}

	void terminate(ExecutionState& state);
private:
	Executor&	exe;
};
}

#endif

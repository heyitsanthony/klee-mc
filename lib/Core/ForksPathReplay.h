#ifndef FORKSPATHREPLAY_H
#define FORKSPATHREPLAY_H

#include "Forks.h"

namespace klee
{
class ExecutionState;
class CallPathNode;
class ExprVisitor;
class KInstruction;

class ForksPathReplay : public Forks
{
public:
	ForksPathReplay(Executor& _exe);
	virtual ~ForksPathReplay() {}
	void setForkSuppress(bool v) { suppressForks = v; }
protected:
	void trackBranch(ExecutionState& current, unsigned);
	void setupForkAffinity(
		ExecutionState& current,
		struct ForkInfo& fi,
		unsigned* cond_idx_map);

	bool forkSetup(ExecutionState& current, struct ForkInfo& fi);

private:
	bool forkFollowReplay(ExecutionState& current, struct ForkInfo& fi);

	bool suppressForks;
};

}

#endif

#ifndef FORKSPATHREPLAY_H
#define FORKSPATHREPLAY_H

#include "Forks.h"

namespace klee
{
class ExecutionState;
class ExprVisitor;
class KInstruction;

class ForksPathReplay : public Forks
{
public:
	ForksPathReplay(Executor& _exe);
	virtual ~ForksPathReplay() {}
protected:
	void trackBranch(ExecutionState& current, unsigned) override;
	bool setupForkAffinity(
		ExecutionState& current,
		const struct ForkInfo& fi,
		unsigned* cond_idx_map) override;

	bool forkSetup(ExecutionState& current, struct ForkInfo& fi) override;

private:
	bool forkFollowReplay(ExecutionState& current, struct ForkInfo& fi);
};

}

#endif

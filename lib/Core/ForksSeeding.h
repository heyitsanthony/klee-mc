#ifndef FORKSSEEDING_H
#define FORKSSEEDING_H

#include "Forks.h"

namespace klee
{
class ForksSeeding : public Forks
{
public:
	ForksSeeding(Executor& exe) : Forks(exe) {}
	virtual ~ForksSeeding() {}
protected:
	bool forkSetup(ExecutionState& current, struct ForkInfo& fi);
	bool evalForkBranch(ExecutionState& current, struct ForkInfo& fi);
	bool constrainFork(
		ExecutionState& current,
		struct ForkInfo& fi,
		unsigned int condIndex);
	
	bool isForkingCondition(ExecutionState& current, ref<Expr> cond);
private:
};
}

#endif

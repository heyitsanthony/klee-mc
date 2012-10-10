#ifndef FORKSKTEST_H
#define FORKSKTEST_H

#include "Forks.h"

struct KTest;

namespace klee
{
class ForksKTest : public Forks
{
public:
	ForksKTest(Executor& exe)
	: Forks(exe), kt(0), kt_assignment(0) {}

	virtual ~ForksKTest();
	void setKTest(const KTest* _kt);
protected:
	bool setupForkAffinity(
		ExecutionState& current,
		struct ForkInfo& fi,
		unsigned* cond_idx_map);

private:
	bool updateSymbolics(ExecutionState& current);
	const KTest	*kt;
	Assignment	*kt_assignment;
	std::vector<ref<Array> > arrs;

};
}

#endif

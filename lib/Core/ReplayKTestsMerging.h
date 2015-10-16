#ifndef REPLAY_KTESTS_MERGE_H
#define REPLAY_KTESTS_MERGE_H

#include <unordered_map>
#include "klee/Replay.h"
#include "CovSet.h"

namespace klee {

class ReplayKTestsMerging : public ReplayKTestsFast
{
public:
	ReplayKTestsMerging(const ktest_list_t& _kts)
		: ReplayKTestsFast(_kts)
	{}

	static bool isReplayMerging(void);

protected:
	ExecutionState* replayKTest(
		Executor& exe, ExecutionState&, const KTest* kt) override;
	bool replayKTests(Executor&, ExecutionState&) override;

private:
	void buildReps(void);
	bool tryMutant(
		Executor& exe,
		ExecutionState& initSt,
		const KTest* ktest_orig,
		const KTest* mutant);

	KTest* mutate(const KTest*);

	// total covered
	std::unordered_map<const KTest*, CovSet::covset_t>	ktest_cov;
	// representative kfunctions; unique to this test
	std::unordered_map<const KTest*, CovSet::covset_t>	ktest_reps;
};

}

#endif

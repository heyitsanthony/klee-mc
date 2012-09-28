#include <llvm/Support/CommandLine.h>
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "ForksPathReplay.h"
#include "StateSolver.h"
#include <sstream>

namespace llvm
{
	llvm::cl::opt<bool>
	ReplaySuppressForks(
		"replay-suppress-forks",
		llvm::cl::desc("On replay, suppress symbolic forks."),
		llvm::cl::init(true));

	llvm::cl::opt<bool>
	ReplayPathOnly(
		"replay-path-only",
		llvm::cl::desc("On replay, kill states on branch exhaustion"));
}

using namespace klee;
using namespace llvm;

ForksPathReplay::ForksPathReplay(Executor& _exe)
: Forks(_exe)
{
	suppressForks = ReplaySuppressForks;
}

void ForksPathReplay::trackBranch(ExecutionState& current, unsigned condIndex)
{
	// no need to track the branch since we're following a replay
	// HOWEVER: must track branch for forked states!
	if (current.isReplayDone()) {
		Forks::trackBranch(current, condIndex);
	}
}

bool ForksPathReplay::forkFollowReplay(ExecutionState& es, struct ForkInfo& fi)
{
	// Replaying non-internal fork; read value from replayBranchIterator
	fi.replayTargetIdx = es.stepReplay();

	// Verify that replay target matches current path constraints
	if (fi.replayTargetIdx > fi.N) {
		exe.terminateOnError(es, "replay out of range", "branch.err");
		// assert (targetIndex <= fi.N && "replay target out of range");
		return false;
	}

	if (fi.res[fi.replayTargetIdx]) {
		// Suppress forking; constraint will be added to path
		// after forkSetup is complete.
		if (suppressForks)
			fi.res.assign(fi.N, false);
		fi.res[fi.replayTargetIdx] = true;

		return true;
	}

	std::stringstream ss;
	ss	<< "hit invalid branch in replay path mode (line="
		<< es.prevPC->getInfo()->assemblyLine
		<< ", prior-path target=" << fi.replayTargetIdx
		<< ", replay targets=";

	bool first = true;
	for(unsigned i = 0; i < fi.N; i++) {
		if (!fi.res[i]) continue;
		if (!first) ss << ",";
		ss << i;
		first = false;
	}
	ss << ")\n";

	fi.dump(ss);

	Query	q(es.constraints, fi.conditions[1]);
	ss	<< "Query hash: " << (void*)q.hash() << '\n';
	exe.terminateOnError(es, ss.str().c_str(), "branch.err");

	klee_warning("hit invalid branch in replay path mode");
	return false;
}

bool ForksPathReplay::forkSetup(ExecutionState& current, struct ForkInfo& fi)
{
	if (!fi.isInternal && current.isCompact()) {
		// Can't fork compact states; sanity check
		assert(false && "invalid state");
	}

	if (ReplayPathOnly && current.isReplay && current.isReplayDone()) {
		// Done replaying this state, so kill it (if -replay-path-only)
		exe.terminateEarly(current, "replay path exhausted");
		return false;
	}

	assert (current.isReplayDone() == false);
	return forkFollowReplay(current, fi);
}

void ForksPathReplay::setupForkAffinity(
	ExecutionState& current,
	struct ForkInfo& fi,
	unsigned* cond_idx_map)
{
	/* steer to expected branch */
	cond_idx_map[0] = fi.replayTargetIdx;
	cond_idx_map[fi.replayTargetIdx] = 0;
}

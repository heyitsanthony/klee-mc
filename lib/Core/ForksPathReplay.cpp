#include <llvm/Support/CommandLine.h>
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "ForksPathReplay.h"
#include "StateSolver.h"
#include "klee/Replay.h"
#include <sstream>

using namespace klee;
using namespace llvm;

ForksPathReplay::ForksPathReplay(Executor& _exe)
: Forks(_exe)
{ suppressForks = Replay::isSuppressForks(); }

void ForksPathReplay::trackBranch(ExecutionState& current, unsigned condIndex)
{
	// no need to track the branch since we're following a replay
	// HOWEVER: must track branch for forked states!
	if (current.isReplayDone()) {
		Forks::trackBranch(current, condIndex);
	} else {
		current.stepReplay();
	}
}

bool ForksPathReplay::forkFollowReplay(ExecutionState& es, struct ForkInfo& fi)
{
	// Replaying non-internal fork; read value from replayBranchIterator
	unsigned	brSeqIdx;

	brSeqIdx = es.getBrSeq();
	fi.replayTargetIdx = es.peekReplay();

	// Verify that replay target matches current path constraints
	if (fi.replayTargetIdx > fi.N) {
		TERMINATE_ERROR(&exe, es, "replay out of range", "branch.err");
		// assert (targetIndex <= fi.N && "replay target out of range");
		return false;
	}

	if (fi.res[fi.replayTargetIdx]) {
		// Suppress forking; constraint will be added to path
		// after forkSetup is complete.
		if (	suppressForks ||
			es.totalInsts < Replay::getMaxSuppressInst())
		{
			fi.res.assign(fi.N, false);
		}
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
	ss << ", seqIdx=" << brSeqIdx << ")\n";

	fi.dump(ss);

	Query	q(es.constraints, fi.conditions[1]);
	ss	<< "Query hash: " << (void*)q.hash() << '\n';
	TERMINATE_ERROR(&exe, es, ss.str().c_str(), "branch.err");

	klee_warning("hit invalid branch in replay path mode");
	return false;
}

bool ForksPathReplay::forkSetup(ExecutionState& current, struct ForkInfo& fi)
{
	if (!fi.isInternal && current.isCompact()) {
		// Can't fork compact states; sanity check
		assert(false && "invalid state");
	}

	if (Replay::isReplayOnly() && current.isReplayDone()) {
		// Done replaying this state, so kill it (if -replay-path-only)
		TERMINATE_EARLY(&exe, current, "replay path exhausted");
		return false;
	}

	if (current.isReplayDone()) {
		/* XXX: this is happens when we have a loop in 
		 * the interpreter branching off a bunch of states; 
		 * several states per state means that we'll fork off
		 * the replayed state, then use non-replay states to fork
		 * the rest. */
		return Forks::forkSetup(current, fi);
	}

	return forkFollowReplay(current, fi);
}

bool ForksPathReplay::setupForkAffinity(
	ExecutionState& current,
	struct ForkInfo& fi,
	unsigned* cond_idx_map)
{
	if (current.isReplayDone()) {
		Forks::setupForkAffinity(current, fi, cond_idx_map);
		return true;
	}

	/* steer to expected branch */
	cond_idx_map[0] = fi.replayTargetIdx;
	cond_idx_map[fi.replayTargetIdx] = 0;
	return true;
}

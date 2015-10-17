#include <llvm/Support/CommandLine.h>
#include <algorithm>
#include <malloc.h>
#include "klee/Common.h"
#include "ExeStateManager.h"
#include "MemUsage.h"
#include "Forks.h"
#include "CoreStats.h"
#include "OOMTimer.h"

llvm::cl::opt<bool>
UsePID("use-pid", llvm::cl::desc("Use proportional state control"));

llvm::cl::opt<unsigned>
MaxMemory("max-memory", llvm::cl::desc("Refuse forks above cap (in MB, 0=off)"));

using namespace klee;

bool OOMTimer::atMemoryLimit = false;

unsigned OOMTimer::getMaxMemory(void) { return MaxMemory; }

void OOMTimer::handleMemoryPID(void)
{
	#define K_P	0.6
	#define K_D	0.1	/* damping factor-- damp changes in errors */
	#define K_I	0.0001  /* systematic error-- negative while ramping  */
	int		states_to_gen;
	int64_t		err;
	uint64_t	mbs;
	static int64_t	err_sum = -(int64_t)MaxMemory;
	static int64_t	last_err = 0;

	mbs = mallinfo().uordblks/(1024*1024);
	err = MaxMemory - mbs;

	states_to_gen = K_P*err + K_D*(err - last_err) + K_I*(err_sum);
	err_sum += err;
	last_err = err;

	if (states_to_gen >= 0)
		return;

	exe.getStateManager()->compactStates(-states_to_gen);
}

// guess at how many to kill
void OOMTimer::killStates(void)
{
	uint64_t numStates = exe.getNumStates();
	uint64_t mbs = getMemUsageMB();
	unsigned toKill = std::max(	(uint64_t)1,
					numStates - (numStates*MaxMemory)/mbs);
	assert (mbs > MaxMemory);

	if (!numStates) {
		return;
	}

	klee_warning("killing %u states (over mem). Total: %ld.", toKill, numStates);

	auto esm = exe.getStateManager();
	std::vector<ExecutionState*> arr(esm->begin(), esm->end());

	// use priority ordering for selecting which states to kill
	std::partial_sort(
		arr.begin(), arr.begin() + toKill, arr.end(), KillOrCompactOrdering());
	for (unsigned i = 0; i < toKill; ++i) {
		TERMINATE_EARLY(&exe, *arr[i], "memory limit");
	}
	klee_message("Killed %u states.", toKill);
}

void OOMTimer::run(void)
{
	uint64_t mbs, instLimit;

	// Avoid calling GetMallocUsage() often; it is O(elts on freelist).
	if (UsePID) {
		handleMemoryPID();
		return;
	}

	mbs = getMemUsageMB();
	if (mbs < 0.9*MaxMemory) {
		atMemoryLimit = false;
		return;
	}

	if (mbs <= MaxMemory) return;

	/*  (mbs > MaxMemory) */
	atMemoryLimit = true;

	if (mbs <= 1.1*MaxMemory)
		return;

	instLimit = stats::instructions - lastMemoryLimitOperationInstructions;
	lastMemoryLimitOperationInstructions = stats::instructions;

	if (	Forks::isReplayInhibitedForks() &&
		instLimit > 0x20000 &&
		exe.getNumStates() > 1)
	{
		std::cerr << "[Exe] Replay inhibited forks.. COMPACTING!!\n";
		exe.getStateManager()->compactPressureStates(MaxMemory);
		return;
	}

	/* resort to killing states if compacting  didn't help memory usage */
	killStates();
}

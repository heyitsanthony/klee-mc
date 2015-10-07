//
// Loads KTests then steers the forking according to the KTest values.
//
// The reason for not plugging the ktest values directly in is that this
// is used for seeding, so it's important to know when states can fork off
// the replay.
//
// XXX: Make more efficient with partial KTest solver.
//
#include <llvm/Support/CommandLine.h>
#include "ForksKTest.h"
#include "Executor.h"
#include "ExeStateManager.h"
#include "PTree.h"
#include "CoreStats.h"
#include "CostKillerStateSolver.h"
#include "klee/SolverStats.h"
#include "static/Sugar.h"
#include "SpecialFunctionHandler.h"
#include <algorithm>

using namespace klee;

extern llvm::cl::opt<bool> FasterReplay;
namespace
{	llvm::cl::opt<bool>
		ReplayKTestSort("replay-ktest-sort",
		llvm::cl::desc("Sort by prefix to reduce redundant states"),
		llvm::cl::init(true));
	llvm::cl::opt<double>
		MaxSolverSeconds("max-ckiller-time",
		llvm::cl::desc(
			"Maximum number of query seconds to replay each ktest"),
		llvm::cl::init(10.0));
}

bool ReplayKTests::replay(Executor* exe, ExecutionState* initSt)
{
	Forks		*old_f;
	StateSolver	*old_ss = exe->getSolver(), *new_ss;
	SFHandler	*old_sfh;
	bool		ret;

	std::cerr << "[KTest] Replaying " << kts.size() << " ktests.\n";

	old_sfh = exe->getSFH()->setFixedHandler("klee_make_vsym", 32, 0);

	old_f = exe->getForking();

	/* timeout expensive replayed states */
	new_ss = new CostKillerStateSolver(old_ss, MaxSolverSeconds);
	exe->setSolver(new_ss);

	ret = (FasterReplay)
		? replayFast(exe, initSt)
		: replaySlow(exe, initSt);
	exe->setForking(old_f);

	exe->setSolver(old_ss);
	delete new_ss;

	exe->getSFH()->addHandler(old_sfh, "klee_make_vsym", true);

	std::cerr << "[KTest] All replays complete.\n";
	return ret;
}

#include "klee/Internal/ADT/KTest.h"


bool ReplayKTests::replayFast(Executor* exe, ExecutionState* initSt)
{
	auto f_ktest = std::make_unique<ForksKTestStateLogger>(*exe);

	exe->setForking(f_ktest.get());

	if (ReplayKTestSort) {
		std::vector<KTest*>	kts_s(kts);
		std::sort(kts_s.begin(), kts_s.end(),
			[] (auto x, auto y) { return *x < *y; } );
		replayFast(exe, initSt, kts_s);
	} else
		replayFast(exe, initSt, kts);

	return true;
}

void ReplayKTests::replayFast(
	Executor* exe,
	ExecutionState* initSt,
	const std::vector<KTest*>& in_kts)
{
	ExeStateManager		*esm = exe->getStateManager();
	ForksKTestStateLogger	*f_ktest;
	unsigned int		i = 0;

	f_ktest = (ForksKTestStateLogger*)exe->getForking();
	for (auto &ktest : in_kts) {
		ExecutionState	*es(f_ktest->getNearState(ktest));
		unsigned	old_qc = stats::queries;

		if (es == NULL) {
			std::cerr << "[ReplayKTest] No near state.\n";
			es = initSt->copy();
			es->ptreeNode->markReplay();
			esm->queueSplitAdd(es->ptreeNode, initSt, es);
		} else {
			std::cerr
				<< "[ReplayKTest] Got near state with "
				<< es->getSymbolics().size()
				<< " objects.\n";
		}

		f_ktest->setKTest(ktest);
		exe->exhaustState(es);

		std::cerr
			<< "[Replay] Replay KTest done st=" << es
			<< ". OutcallQueries=" << stats::queries - old_qc
			<< ". Total=" << esm->numRunningStates()
			<< ". (" << ++i << " / " << in_kts.size() << ")\n";
	}
}

bool ReplayKTests::replaySlow(Executor* exe, ExecutionState* initSt)
{
	ExeStateManager	*esm = exe->getStateManager();
	auto f_ktest = std::make_unique<ForksKTest>(*exe);

	exe->setForking(f_ktest.get());
	for (auto const ktest : kts) {
		ExecutionState	*es;

		es = initSt->copy();
		es->ptreeNode->markReplay();
		esm->queueSplitAdd(es->ptreeNode, initSt, es);
		f_ktest->setKTest(ktest);
		exe->exhaustState(es);

		std::cerr
			<< "[Replay] Replay KTest done st="
			<< es << ". Total="
			<< esm->numRunningStates() << "\n";

	}

	return true;
}

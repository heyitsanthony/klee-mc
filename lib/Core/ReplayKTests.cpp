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
#include "klee/Internal/ADT/KTest.h"
#include "klee/KleeHandler.h"
#include <algorithm>

using namespace klee;

llvm::cl::opt<bool>
	ReplayKTestSort("replay-ktest-sort",
		llvm::cl::desc("Sort by prefix to reduce redundant states"),
		llvm::cl::init(true));
llvm::cl::opt<double>
	MaxSolverSeconds("max-ckiller-time",
	llvm::cl::desc(
		"Maximum number of query seconds to replay each ktest"),
	llvm::cl::init(10.0));

llvm::cl::list<std::string>
	ReplayKTestFile(
		"replay-ktest",
		llvm::cl::desc("Specify a ktest file to replay"),
		llvm::cl::value_desc("ktest file"));

llvm::cl::list<std::string>
	ReplayKTestDir(
		"replay-ktest-dir",
		llvm::cl::desc("Specify a directory to replay .ktest files from"),
		llvm::cl::value_desc("ktest directory"));

bool Replay::isReplayingKTest(void) {
	return !ReplayKTestDir.empty() || !ReplayKTestFile.empty();
}

std::vector<KTest*> Replay::loadKTests(void)
{
	std::vector<KTest*>	ret;

	assert (Replay::isReplayingKTest());

	std::vector<std::string>	outDirs(
		ReplayKTestDir.begin(),
		ReplayKTestDir.end()),
					outFiles(
		ReplayKTestFile.begin(),
		ReplayKTestFile.end());

	KleeHandler::getKTests(outFiles, outDirs, ret);
	return ret;
}



ReplayKTests* ReplayKTests::create(const std::vector<KTest*>& _kts)
{
	auto	kts_s(_kts);
	if (ReplayKTestSort) {
		std::sort(kts_s.begin(), kts_s.end(),
			[] (auto x, auto y) { return *x < *y; } );
	}

	if (Replay::isFasterReplay())
		return new ReplayKTestsFast(kts_s);

	return new ReplayKTestsSlow(kts_s);
}

bool ReplayKTests::replay(Executor* exe, ExecutionState* initSt)
{
	Forks		*old_f = exe->getForking(), *new_f;
	StateSolver	*old_ss = exe->getSolver(), *new_ss;
	SFHandler	*old_sfh;
	bool		ret;

	std::cerr << "[KTest] Replaying " << kts.size() << " ktests.\n";

	old_sfh = exe->getSFH()->setFixedHandler("klee_make_vsym", 32, 0);

	/* timeout expensive replayed states */
	new_ss = new CostKillerStateSolver(old_ss, MaxSolverSeconds);
	new_f = createForking(*exe);
	exe->setSolver(new_ss);
	exe->setForking(new_f);

	ret = replayKTests(*exe, *initSt);

	exe->setForking(old_f);
	exe->setSolver(old_ss);
	delete new_f;
	delete new_ss;

	exe->getSFH()->addHandler(old_sfh, "klee_make_vsym", true);

	std::cerr << "[KTest] All replays complete.\n";
	return ret;
}

bool ReplayKTests::replayKTests(Executor& exe, ExecutionState& initSt)
{
	unsigned int i = 0;
	for (auto &ktest : kts) {
		unsigned old_qc = stats::queries;
		auto es = replayKTest(exe, initSt, ktest);
		std::cerr
			<< "[Replay] Replay KTest done st=" << es
			<< ". OutcallQueries=" << stats::queries - old_qc
			<< ". Total=" << exe.getNumStates()
			<< ". (" << ++i << " / " << kts.size() << ")\n";

	}
	return true;
}

Forks* ReplayKTestsFast::createForking(Executor& exe) const {
	return new ForksKTestStateLogger(exe);
}

ExecutionState* ReplayKTestsFast::replayKTest(
	Executor& exe,
	ExecutionState& initSt,
	const KTest* ktest)
{
	auto f_ktest = (ForksKTestStateLogger*)exe.getForking();
	auto esm = exe.getStateManager();
	auto es = f_ktest->getNearState(ktest);

	if (es == nullptr) {
		std::cerr << "[ReplayKTest] No near state.\n";
		es = initSt.copy();
		es->ptreeNode->markReplay();
		esm->queueSplitAdd(es->ptreeNode, &initSt, es);
	} else {
		std::cerr
			<< "[ReplayKTest] Got near state with "
			<< es->getSymbolics().size()
			<< " objects.\n";
		near_c++;
	}

	f_ktest->setKTest(ktest);
	exe.exhaustState(es);
	return es;
}

Forks* ReplayKTestsSlow::createForking(Executor& exe) const {
	return new ForksKTest(exe);
}

ExecutionState* ReplayKTestsSlow::replayKTest(
	Executor& exe,
	ExecutionState& initSt,
	const KTest* ktest)
{
	auto f_ktest = (ForksKTest*)exe.getForking();
	auto esm = exe.getStateManager();
	auto es = initSt.copy();

	es->ptreeNode->markReplay();
	esm->queueSplitAdd(es->ptreeNode, &initSt, es);

	f_ktest->setKTest(ktest);
	exe.exhaustState(es);
	return es;
}



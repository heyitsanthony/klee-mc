#include <llvm/Support/CommandLine.h>
#include "ForksKTest.h"
#include "Executor.h"
#include "ExeStateManager.h"
#include "PTree.h"
#include "static/Sugar.h"

using namespace klee;

extern llvm::cl::opt<bool> FasterReplay;

bool ReplayKTests::replay(Executor* exe, ExecutionState* initSt)
{
	Forks		*old_f;
	bool		ret;

	old_f = exe->getForking();
	ret = (FasterReplay)
		? replayFast(exe, initSt)
		: replaySlow(exe, initSt);
	exe->setForking(old_f);

	return ret;
}

bool ReplayKTests::replayFast(Executor* exe, ExecutionState* initSt)
{
	ExeStateManager		*esm = exe->getStateManager();
	ForksKTestStateLogger	*f_ktest = new ForksKTestStateLogger(*exe);

	exe->setForking(f_ktest);
	foreach (it, kts.begin(), kts.end()) {
		ExecutionState	*es;
		const KTest	*ktest(*it);

		es = f_ktest->getNearState(ktest);
		if (es == NULL) {
			std::cerr << "[ReplayKTest] No near state.\n";
			es = initSt->copy();
			es->ptreeNode->markReplay();
			esm->queueSplitAdd(es->ptreeNode, initSt, es);
		} else {
			std::cerr
				<< "[ReplayKTest] Got near state with "
				<< es->getNumSymbolics()
				<< " objects.\n";
		}

		f_ktest->setKTest(ktest);
		exe->exhaustState(es);

		std::cerr
			<< "[Replay] Replay KTest done st="
			<< es << ". Total="
			<< esm->numRunningStates() << "\n";
	}

	delete f_ktest;

	return true;
}

bool ReplayKTests::replaySlow(Executor* exe, ExecutionState* initSt)
{
	ExeStateManager	*esm = exe->getStateManager();
	ForksKTest	*f_ktest = new ForksKTest(*exe);

	exe->setForking(f_ktest);
	foreach (it, kts.begin(), kts.end()) {
		ExecutionState	*es;
		const KTest	*ktest(*it);

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

	delete f_ktest;

	return true;
}

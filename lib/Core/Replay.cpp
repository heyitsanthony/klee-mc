#include <llvm/Support/CommandLine.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include "static/Sugar.h"
#include "Executor.h"
#include "ExeStateManager.h"
#include "Forks.h"
#include "ForksPathReplay.h"
#include "ForksKTest.h"
#include "CoreStats.h"
#include "PTree.h"

#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KInstIterator.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/ADT/zfstream.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Replay.h"

using namespace klee;

#define DECL_OPTBOOL2(x,y,z) llvm::cl::opt<bool> x(y, llvm::cl::init(z))
#define DECL_OPTBOOL(x,y) DECL_OPTBOOL2(x, y, false)

DECL_OPTBOOL2(ReplaySuppressForks, "replay-suppress-forks", true);
DECL_OPTBOOL(CompleteReplay, "replay-complete");
DECL_OPTBOOL(FasterReplay, "replay-faster");


bool Replay::isSuppressForks(void) { return ReplaySuppressForks; }

// load a .path file
#define IFSMODE	std::ios::in | std::ios::binary
void Replay::loadPathFile(const std::string& name, ReplayPath& buffer)
{
	std::istream	*is;

	if (name.substr(name.size() - 3) == ".gz") {
		std::string new_name = name.substr(0, name.size() - 3);
		is = new gzifstream(name.c_str(), IFSMODE);
	} else
		is = new std::ifstream(name.c_str(), IFSMODE);

	if (is == NULL || !is->good()) {
		assert(0 && "unable to open path file");
		if (is) delete is;
		return;
	}

	loadPathStream(*is, buffer);

	delete is;
}

void Replay::loadPathStream(std::istream& is, ReplayPath& buffer)
{
	while (is.good()) {
		uint64_t	value, id;

		/* get the value */
		is >> std::dec >> value;
		if (!is.good()) {
			std::cerr
				<< "[Replay] Path of size="
				<< buffer.size() << ".\n";
			break;
		}

		/* eat the comma */
		is.get();

		/* get the location */
		is >> std::hex >> id;

		/* but for now, ignore it */
		id = 0;

		/* XXX: need to get format working right for this. */
		buffer.push_back(ReplayNode(value,(const KInstruction*)id));
	}
}

typedef std::map<const llvm::Function*, uint64_t> f2p_ty;

void Replay::writePathFile(const ExecutionState& st, std::ostream& os)
{
	f2p_ty		f2ptr;
	std::string	fstr;

	if (st.branchesBegin() == st.branchesEnd()) {
		std::cerr << "WARNING: Replay without branches\n";
	}

	foreach(bit, st.branchesBegin(), st.branchesEnd()) {
		const KInstruction	*ki;
		const llvm::Function	*f;
		f2p_ty::iterator	fit;
		uint64_t		v;

		os << (*bit).first;

		ki = (*bit).second;
		if (ki == NULL) {
			os << ",0\n";
			continue;
		}

		/* this is klee-mc specific-- should probably support
		 * llvm bitcode here eventually too */
		f = ki->getFunction();
		fit = f2ptr.find(f);
		if (fit != f2ptr.end()) {
			os << ',' << (void*)fit->second  << '\n';
			continue;
		}

		fstr = f->getName().str();
		if (fstr.substr(0, 3) != "sb_") {
			v = 0;
		} else {
			v = strtoul(fstr.substr(3).c_str(), NULL, 16);
			assert (v != 0);
		}

		f2ptr[f] = v;
		os << ',' << ((void*)v) << '\n';
	}
}

static ExecutionState* findClosestState(
	const ReplayPath& rp, const ExeStateManager& esm)
{
	ExecutionState	*best_es = NULL;
	unsigned	best_c = 0;

	foreach (it, esm.begin(), esm.end()) {
		ExecutionState		*es(*it);
		unsigned		es_c = 0;

		es_c = es->replayHeadLength(rp);
		if (es_c > best_c) {
			std::cerr << "[Replay] Best Count = " << es_c << ".\n";
			best_c = es_c;
			best_es = es;
		}
	}

	if (best_c) {
		std::cerr << "BEST_C = " << best_c << '\n';
	}

	return best_es;
}

void ReplayBrPaths::fastEagerReplay(void)
{
	std::list<ExecutionState*>		replay_states;
	const std::list<ReplayPath>::iterator	it;

	std::cerr << "[Replay] Replaying paths FAST!\n";

	/* play every path */
	foreach (it, rps.begin(), rps.end()){
		ReplayPath	rp(*it);
		ExecutionState	*es = NULL;

		es = findClosestState(rp, *esm);
		if (es == NULL) {
			std::cerr << "[Replay] Couldn't hitch partial state\n";
			es = ExecutionState::createReplay(*initState, rp);
			esm->queueSplitAdd(es->ptreeNode, initState, es);
		} else {
			std::cerr << "[Replay] Hitched a partial state\n";
			es = exe->getForking()->pureFork(*es);
			assert (es != NULL);
			es->joinReplay(rp);
			assert (es->isReplayDone() == false);
		}

		assert (es != NULL);

		std::cerr << "[Replay] Replaying " << rp.size() << " branches.\n";
		exe->exhaustState(es);

		std::cerr
			<< "[Replay] Replay state done st="
			<< es << ". Total="
			<< esm->numRunningStates() << "\n";
	}


	std::cerr << "[Replay] All paths replayed. Expanded states: "
		<< esm->numRunningStates() << "\n";
}

bool Replay::verifyPath(Executor* exe, const ExecutionState& es)
{
	Forks			*old_f;
	ForksPathReplay		*fpr;
	ExecutionState		*rp_es, *initSt;
	ReplayPath		rp;
	bool			old_write, failed_rp;
	std::stringstream	 ss;
	unsigned		old_err_c;
	InterpreterHandler	*ih;
	uint64_t		inst_start, inst_end;

	if (es.isPartial) {
		std::cerr << "[Replay] Ignoring partial path check.\n";
		return true;
	}

	if (es.concretizeCount > 0) {
		/* we should only replay up to the point of concretization,
		 and check there are no contradictions up to that point.
		 Anything past the concretization is unreliable since branches 
		 which are safe-guarded by a is-const check will not be visible
		 on subsequent runs without precise concretization replay. */
		std::cerr << "[Replay] Ignoring concretized validation!!!\n";
		return true;
	}

	old_f = exe->getForking();
	ih = exe->getInterpreterHandler();
	old_write = ih->isWriteOutput();
	/* don't write result (XXX: or should I?) */
	ih->setWriteOutput(false);

	fpr = new ForksPathReplay(*exe);
	fpr->setForkSuppress(true);
	exe->setForking(fpr);

	writePathFile(es, ss);
	loadPathStream(ss, rp);

	initSt = exe->getInitialState();
	rp_es = ExecutionState::createReplay(*initSt, rp);
	exe->getStateManager()->queueSplitAdd(rp_es->ptreeNode, initSt, rp_es);

	old_err_c = ih->getNumErrors();
	inst_start = stats::instructions;

	exe->exhaustState(rp_es);

	failed_rp = ih->getNumErrors() != old_err_c;
	inst_end = stats::instructions;

	std::cerr << "[Replay] Exhaust insts=" << inst_end - inst_start << '\n';
	std::cerr << "[Relay] Source insts=" << es.totalInsts << '\n';
	assert ((inst_end - inst_start) == es.totalInsts);

	/* XXX: need some checks to make sure path succeeded */
	assert (!failed_rp && "REPLAY FAILED. NEED BETTER HANDLING");
	if (failed_rp) {
		return false;
	}

	exe->setForking(old_f);
	delete fpr;

	ih->setWriteOutput(old_write);
	std::cerr << "[Replay] Validated Path.\n";

	return true;
}

bool ReplayKTests::replay(Executor* exe, ExecutionState* initSt)
{
	ExeStateManager	*esm = exe->getStateManager();
	ForksKTest	*f_ktest = new ForksKTest(*exe);
	Forks		*old_f;

	old_f = exe->getForking();
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
	exe->setForking(old_f);

	return true;
}

ReplayList::~ReplayList() { foreach (it, rps.begin(), rps.end()) delete (*it); }

bool ReplayList::replay(Executor* exe, ExecutionState* initSt)
{
	foreach (it, rps.begin(), rps.end())
		if ((*it)->replay(exe, initSt) == false)
			return false;

	return true;
}


bool ReplayBrPaths::replay(Executor* _exe, ExecutionState* _initState)
{
	Forks	*old_forking, *rp_forking;

	exe = _exe;
	initState = _initState;
	esm = exe->getStateManager();
	assert (esm);

	old_forking = exe->getForking();
	rp_forking = new ForksPathReplay(*exe);
	exe->setForking(rp_forking);

	assert (initState->ptreeNode != NULL);

	eagerReplayPathsIntoStates();

	/* complete replay => will try new paths */
	if (CompleteReplay == false) {
		incompleteReplay();
	}

	exe->setForking(old_forking);
	delete rp_forking;

	return true;
}

void ReplayBrPaths::incompleteReplay(void)
{
	esm->queueRemove(initState);
	esm->commitQueue();
}

/* DEPRECATED */
void ReplayBrPaths::delayedReplayPathsIntoStates()
{
	foreach (it, rps.begin(), rps.end()) {
		ExecutionState *es;
		es = ExecutionState::createReplay(*initState, (*it));
		esm->queueSplitAdd(es->ptreeNode, initState, es);
	}
}

void ReplayBrPaths::eagerReplayPathsIntoStates(void)
{
	std::list<ExecutionState*>      replay_states;

	std::cerr << "[Replay] Eagerly replaying paths.\n";

	if (FasterReplay) {
		fastEagerReplay();
		return;
	}

	/* create paths */
	foreach (it, rps.begin(), rps.end()) {
		ExecutionState	*es;
		es = ExecutionState::createReplay(*initState, (*it));
		esm->queueSplitAdd(es->ptreeNode, initState, es);
		replay_states.push_back(es);
	}

	/* replay paths */
	std::cerr << "[Executor] Replaying paths.\n";
	foreach (it, replay_states.begin(), replay_states.end()) {
		ExecutionState  *es = *it;
		exe->exhaustState(es);
		std::cerr << "[Executor] Replay state done st=" << es << ".\n";
	}

	std::cerr << "[Executor] All paths replayed. Expanded states: "
		<< esm->numRunningStates() << "\n";
}



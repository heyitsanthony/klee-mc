#include <llvm/Support/CommandLine.h>
#include <iostream>
#include <fstream>
#include "static/Sugar.h"
#include "Executor.h"
#include "ExeStateManager.h"
#include "Forks.h"
#include "ForksPathReplay.h"

#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KInstIterator.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/ADT/zfstream.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Replay.h"

using namespace klee;

#define DECL_OPTBOOL(x,y) llvm::cl::opt<bool> x(y, llvm::cl::init(false))
DECL_OPTBOOL(CompleteReplay, "replay-complete");
DECL_OPTBOOL(FasterReplay, "replay-faster");

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

	while (is->good()) {
		uint64_t	value, id;

		/* get the value */
		*is >> std::dec >> value;
		if (!is->good()) {
			std::cerr
				<< "[Replay] Path of size="
				<< buffer.size() << ".\n";
			break;
		}

		/* eat the comma */
		is->get();

		/* get the location */
		*is >> std::hex >> id;

		/* but for now, ignore it */
		id = 0;

		/* XXX: need to get format working right for this. */
		buffer.push_back(ReplayNode(value,(const KInstruction*)id));
	}

	delete is;
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

void Replay::fastEagerReplay(void)
{
	std::list<ExecutionState*>		replay_states;
	const std::list<ReplayPath>::iterator	it;

	std::cerr << "[Replay] Replaying paths FAST!\n";

	/* play every path */
	foreach (it, replayPaths.begin(), replayPaths.end()){
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
		}

		assert (es != NULL);

		exe->exhaustState(es);

		std::cerr
			<< "[Replay] Replay state done st="
			<< es << ". Total="
			<< esm->numRunningStates() << "\n";
	}


	std::cerr << "[Replay] All paths replayed. Expanded states: "
		<< esm->numRunningStates() << "\n";
}

void Replay::eagerReplayPathsIntoStates()
{
	std::list<ExecutionState*>      replay_states;

	std::cerr << "[Replay] Eagerly replaying paths.\n";

	if (FasterReplay) {
		fastEagerReplay();
		return;
	}

	/* create paths */
	foreach (it, replayPaths.begin(), replayPaths.end()) {
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

void Replay::replayPathsIntoStates(
	Executor		*exe,
	ExecutionState		*initialState,
	const ReplayPaths	&rps)
{
	Replay	rp(exe, initialState, rps);

	assert (initialState->ptreeNode != NULL);

	rp.eagerReplayPathsIntoStates();

	/* complete replay => will try new paths */
	if (CompleteReplay == false) {
		rp.incompleteReplay();
	}
}

void Replay::incompleteReplay(void)
{
	esm->queueRemove(initState);
	esm->commitQueue();
}

/* DEPRECATED */
void Replay::delayedReplayPathsIntoStates()
{
	foreach (it, replayPaths.begin(), replayPaths.end()) {
		ExecutionState *es;
		es = ExecutionState::createReplay(*initState, (*it));
		esm->queueSplitAdd(es->ptreeNode, initState, es);
	}
}

Replay::Replay(Executor* _exe, ExecutionState* _initState, const ReplayPaths& rps)
: exe(_exe)
, initState(_initState)
, replayPaths(rps)
{
	esm = exe->getStateManager();
	assert (esm);
}

bool Replay::verifyPath(Executor* exe, const ExecutionState& es)
{
	Forks		*old_f;
	ForksPathReplay	*fpr;
	ExecutionState	*replay_es, *initSt;
	ReplayPath	rp;
	bool		old_write;

	old_f = exe->getForking();
	old_write = exe->getInterpreterHandler()->isWriteOutput();
	/* don't write result (XXX: or should I?) */
	exe->getInterpreterHandler()->setWriteOutput(false);

	fpr = new ForksPathReplay(*exe);
	fpr->setForkSuppress(true);
	exe->setForking(fpr);

	/* XXX: make this suck less */
	{
	std::ofstream of("verify.path");
	writePathFile(es, of);
	}
	loadPathFile("verify.path", rp);

	initSt = exe->getInitialState();
	replay_es = ExecutionState::createReplay(*initSt, rp);
	exe->getStateManager()->queueSplitAdd(
		replay_es->ptreeNode, initSt, replay_es);
	exe->exhaustState(replay_es);

	/* XXX: need some checks to make sure path succeeded */
	assert (0 == 1 && "STUB");

	exe->setForking(old_f);
	delete fpr;

	exe->getInterpreterHandler()->setWriteOutput(old_write);
}

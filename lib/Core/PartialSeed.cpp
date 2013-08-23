#include <llvm/Support/CommandLine.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <iostream>
#include <sstream>
#include "klee/Internal/ADT/Hash.h"
#include "klee/Internal/Module/KFunction.h"
#include "klee/Internal/Module/KModule.h"

#include "klee/SolverStats.h"
#include "StateSolver.h"
#include "KTestStateSolver.h"
#include "ForksKTest.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/Internal/Support/Timer.h"
#include "klee/Internal/ADT/zfstream.h"
#include "klee/KleeHandler.h"
#include "static/Sugar.h"

#include "SpecialFunctionHandler.h"
#include "Executor.h"


llvm::cl::opt<std::string> PartSeedDB("psdb-dir", llvm::cl::init("psdb"));
llvm::cl::opt<bool> PartSeedReplay("psdb-replay");

using namespace klee;

typedef uint64_t psid_t;
typedef std::pair<std::string, ExecutionState*>		partseed_t;
typedef std::map<psid_t, partseed_t>			psmap_t;
typedef std::map<std::string, std::vector<unsigned> >	psdb_t;
static psmap_t psmap;


static void getPSDBLib(
	Executor* exe,
	const std::string& name, std::ostream& os)
{
	std::string		modname, h;
	const KFunction		*kf;

	kf = exe->getKModule()->getPrettyFunc(name.c_str());
	if (kf == NULL)
		kf = exe->getKModule()->getPrettyFunc((name + "+0x0").c_str());

	if (kf != NULL) modname = kf->getModName().c_str();
	h = Hash::SHA((const unsigned char*)modname.c_str(), modname.size());
	os << PartSeedDB << '/' << h << '/';
}

static void getPSDBPath(
	Executor* exe,
	const std::string& name, std::ostream& os)
{
	getPSDBLib(exe, name, os);
	os << name << '/';
}


static int dummy_depth_c = 0;
static bool old_fork_suppress;
static uint64_t last_sid = 0;

SFH_DEF_ALL(PartSeedBeginDummy, "klee_partseed_begin", true)
{
	Forks	*f(sfh->executor->getForking());

	if (last_sid != state.getSID()) {
		f->setForkSuppress(old_fork_suppress);
		dummy_depth_c = 0;
	}

	if (dummy_depth_c == 0) {
		old_fork_suppress = f->getForkSuppress();
		f->setForkSuppress(true);
		last_sid = state.getSID();
	}

	dummy_depth_c++;
	state.bindLocal(target, MK_CONST(0xdeadbeef, 64));
}

SFH_DEF_ALL(PartSeedEndDummy, "klee_partseed_end", false)
{
	Forks	*f(sfh->executor->getForking());
	dummy_depth_c--;
	if (dummy_depth_c != 0) return;
	f->setForkSuppress(old_fork_suppress);
}

SFH_DEF_ALL(PartSeedBeginCollect, "klee_partseed_begin", true)
{
	SFH_CHK_ARGS(1, "klee_partseed_begin");

	static psid_t	psid_c = 0;
	psid_t		cur_psid;
	std::string	name;

	name = sfh->readStringAtAddress(state, args[0]);
	cur_psid = ++psid_c;
	psmap[cur_psid] = partseed_t(name, state.copy());
	state.bindLocal(target, MK_CONST(cur_psid, 64));

	std::cerr << "[PS] Entering psid " << cur_psid << " with " <<
		state.getNumSymbolics() << " objects\n";
}

typedef std::pair<std::string, unsigned> psdelta_t;

SFH_DEF_ALL(PartSeedEndCollect, "klee_partseed_end", false)
{
	SFH_CHK_ARGS(1, "klee_partseed_end");

	const ConstantExpr		*ce;
	psmap_t::iterator		it;
	unsigned			inst_delta;
	static std::set<psdelta_t>	seen_deltas;
	KleeHandler::out_objs		objs;
	unsigned			n;

	ce = dyn_cast<ConstantExpr>(args[0]);
	assert (ce != NULL && "expected constant psid");

	if ((it = psmap.find(ce->getZExtValue())) == psmap.end()) {
		std::cerr << "[PS] Couldn't find psid; maybe poisoned.\n";
		return;
	}

	/* find state difference */
	partseed_t		ps(it->second);

	/* new symbolics? */
	if ((state.getNumSymbolics() - ps.second->getNumSymbolics()) <= 0) {
		/* this is a poisoning policy-- if there are no new symbolics
		 * then it depends on inputs and won't cache well. */
		delete ps.second;
		psmap.erase(it);

		std::cerr << "[PS] No symbolic object delta (objs="
			<< state.getNumSymbolics()
			<< "). Ignoring(KILLING)\n";

		sfh->executor->terminate(state);
		return;
	}

	inst_delta = state.totalInsts - ps.second->totalInsts;
	psdelta_t	psd(ps.first, inst_delta);

	/* avoid duplicates */
	if (seen_deltas.count(psd)) {
		std::cerr << "[PS] Seen this instr count. Killing.\n";
		sfh->executor->terminate(state);
		return;
	}

	/* find solution to partial seed objcets */
	if (sfh->executor->getSymbolicSolution(state, objs) == false) {
		std::cerr << "[PS] Couldn't solve.\n";
		return;
	}

	/* erase # objects at start of partseed; they are irrelevant */
	n = 0;
	foreach (it2, state.symbolicsBegin(), state.symbolicsEnd()) {
		n++;
		if (n <= ps.second->getNumSymbolics()) {
			objs.erase(objs.begin());
			continue;
		}
	}

	seen_deltas.insert(psd);

	std::cerr
		<< "[PS] Dumping test "
		<< ps.first << " @ " << inst_delta << '\n';

	/* save data */
	KleeHandler		*kh;
	std::ostream		*f;
	std::stringstream	ss;

	/* XXX: smarter way to do this? hashes? */
	kh = static_cast<KleeHandler*>(sfh->executor->getInterpreterHandler());
	mkdir(PartSeedDB.c_str(), 0755);

	getPSDBLib(sfh->executor, ps.first, ss);
	mkdir(ss.str().c_str(), 0755);

	ss << ps.first << '/';
	mkdir(ss.str().c_str(), 0755);


	ss << inst_delta << ".ktest.gz";
	f = new gzofstream(
		ss.str().c_str(),
		std::ios::out | std::ios::trunc | std::ios::binary);
	if (f != NULL) {
		kh->processSuccessfulTest(f, objs);
		delete f;
	}
}


static Forks* old_fork = NULL;
namespace llvm { extern cl::opt<bool> ConcretizeEarlyTerminate; }


class PSDBFunc
{
public:
private:
};


static bool loadPSDB(
	psdb_t& psdb,
	const std::string& name,
	const std::string dirname)
{
	/* not found; load psdb instruction counts for 'name' */
	DIR			*d;
	struct dirent		*de;
	std::vector<unsigned>	insts_c;

	d = opendir(dirname.c_str());
	if (d == NULL)
		return false;

	while ((de = readdir(d)) != NULL) {
		unsigned	inst_c;

		if (!sscanf(de->d_name, "%u.ktest.gz", &inst_c))
			continue;

		insts_c.push_back(inst_c);
	}

	closedir(d);

	std::sort(insts_c.begin(), insts_c.end());
	std::cerr << "[PS] Loaded " << insts_c.size()
		<< " paths from " << name << '\n';
	psdb[name] = insts_c;

	return true;
}

SFH_DEF_ALL(PartSeedBeginReplay, "klee_partseed_begin", true)
{
	std::string		name;
	std::stringstream	ss;
	static psdb_t		psdb;
	static int		recur_depth = 0;
	unsigned		sel_idx;
	ForksKTest		*fork_ktest;
	KTest			*kt;
	psdb_t::iterator	it;

	if (recur_depth == 1) {
		std::cerr << "[PS] Avoiding recursion.\n";
		state.bindLocal(target, MK_CONST(0xdeadbeef, 64));
		return;
	}

	name = sfh->readStringAtAddress(state, args[0]);
	getPSDBPath(sfh->executor, name, ss);
	
	it = psdb.find(name);
	if (it == psdb.end()) {
		if (loadPSDB(psdb, name, ss.str()) == false) {
			std::cerr << "[PS] Could not open " << name << '\n';
			state.bindLocal(target, MK_CONST(0xdeadbeef, 64));
			return;
		}
		it = psdb.find(name);
	}

	if (it->second.empty()) {
		state.bindLocal(target, MK_CONST(0xdeadbeef, 64));
		return;
	}

	/* random selection */
	/* XXX better selection policy */
	sel_idx = it->second[rand() % it->second.size()];
	ss << sel_idx << ".ktest.gz";
	kt = kTest_fromFile(ss.str().c_str());
	if (kt == NULL) {
		std::cerr << "[PS] Could not open ktest " << ss.str() << '\n';
		state.bindLocal(target, MK_CONST(0xdeadbeef, 64));
		return;
	}

	recur_depth++;
	
	std::cerr << "[PS] Loaded ktest " << ss.str() << '\n';
	state.bindLocal(target, MK_CONST(sel_idx, 64));
	fork_ktest = new ForksKTest(*sfh->executor);
	fork_ktest->setMakeErrTests(false);
	fork_ktest->setKTest(kt, &state);
	fork_ktest->setForkSuppress(true);
	fork_ktest->setConstraintOmit(false);


	/* since I suppress, make sure that a failed replay doesn't sink path */
	/* XXX: dynamic adjustments-- 1/2 and 1/4 rates seem good for now */
	if (state.newInsts || (rand() % 4) == 0) {
		ExecutionState	*new_state;
		new_state = sfh->executor->pureFork(state, false);
		new_state->abortInstruction();
	}

	old_fork = sfh->executor->getForking();
	sfh->executor->setForking(fork_ktest);

	StateSolver	*old_ss, *new_ss;
	bool		old_cet;
	
	old_cet = llvm::ConcretizeEarlyTerminate;
	llvm::ConcretizeEarlyTerminate = false; 

	old_ss = sfh->executor->getSolver();
	new_ss = new KTestStateSolver(old_ss, state, kt);
	sfh->executor->setSolver(new_ss);

	WallTimer	wt;
	int		st_c_old(sfh->executor->getNumStates());
	unsigned	tq_c_old(stats::queriesTopLevel);
	unsigned	rq_c_old(StateSolver::getRealQueries());

	/* XXX: This makes the executor is reentrant. Should I be worried? */
	sfh->executor->exhaustState(&state);

	std::cerr << "[PS] STATES=" << sfh->executor->getNumStates() - st_c_old
		<< ". Queries=" << StateSolver::getRealQueries() - rq_c_old
		<< ". TQs=" << stats::queriesTopLevel - tq_c_old
		<< ". CheapForks=" << fork_ktest->getCheapForks()
		<< ". Time=" << wt.checkSecs() << "s\n";
	std::cerr << "[PS] Done seeding.\n";

	llvm::ConcretizeEarlyTerminate = old_cet;
	sfh->executor->setForking(old_fork);
	sfh->executor->setSolver(old_ss);
	delete new_ss;
	delete fork_ktest;
	kTest_free(kt);

	recur_depth--;
}

SFH_DEF_ALL(PartSeedEndReplay, "klee_partseed_end", false)
{
	ExecutionState*		fork_es;
	const ConstantExpr*	ce;

	std::cerr << "[PS] Got partseed end.\n";
	EXPECT_CONST("klee_partseed_end", ce, 0);

	if (ce->getZExtValue() == 0xdeadbeef) {
		std::cerr << "[PS] Ignoring nested partial seed\n";
		return;
	}

	if (old_fork == NULL) {
		std::cerr << "[PS] No old fork?\n";
		return;
	}

	/* this bumps the number of paths explored so exhaust terminates */
	KleeHandler	*kh;
	kh = static_cast<KleeHandler*>(sfh->executor->getInterpreterHandler());
	kh->incPathsExplored();

	sfh->executor->setForking(old_fork);
}

namespace klee
{
void PartSeedSetupDummy(Executor* exe)
{
	SpecialFunctionHandler	*sfh(exe->getSFH());
	sfh->addHandler(HandlerPartSeedBeginDummy::hinfo);
	sfh->addHandler(HandlerPartSeedEndDummy::hinfo);
}

void PartSeedSetup(Executor* exe)
{
	SpecialFunctionHandler	*sfh(exe->getSFH());
	if (PartSeedReplay) {
		sfh->addHandler(HandlerPartSeedBeginReplay::hinfo);
		sfh->addHandler(HandlerPartSeedEndReplay::hinfo);
	} else {
		sfh->addHandler(HandlerPartSeedBeginCollect::hinfo);
		sfh->addHandler(HandlerPartSeedEndCollect::hinfo);
	}
}
}

/*TODO FIX FAILURE WATERMARKING SO IT'S BASED ON # OF PARTIAL SEEDS */

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
/* <name, list of instruction lengths */
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
		state.getSymbolics().size() << " objects\n";
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
	if ((state.getSymbolics().size() - ps.second->getSymbolics().size()) <= 0) {
		/* this is a poisoning policy-- if there are no new symbolics
		 * then it depends on inputs and won't cache well. */
		delete ps.second;
		psmap.erase(it);

		std::cerr << "[PS] No symbolic object delta (objs="
			<< state.getSymbolics().size()
			<< "). Ignoring\n";//(KILLING)\n";

	//	sfh->executor->terminate(state);
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

	/* XXX: this is garbage with vsyms because fini has to run
	 * first before anything can be done with the solution set.
	 *
	 * I think the right logic here is to kill the state
	 * with a special Terminator that will
	 * 	1) run the full fini list
	 * 	2) dump out the ktest file in the place we want it.
	 *
	 * Too much?
	 * */

	/* find solution to partial seed objcets */
	if (sfh->executor->getSymbolicSolution(state, objs) == false) {
		std::cerr << "[PS] Couldn't solve.\n";
		return;
	}

	/* erase # objects at start of partseed; they are irrelevant */
	n = 0;
	for (unsigned i = 0; i < state.getSymbolics().size(); i++) {
		n++;
		if (n <= ps.second->getSymbolics().size()) {
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


#define FAILURE_WATERMARK	100
#define FAILURE_STK_WATERMARK	10

typedef std::map<std::string, unsigned> failmap_ty;
typedef std::map<uint64_t, unsigned> fm2_ty;

static failmap_ty	fm, fm_total;
static fm2_ty		fm2, fm2_total;


static bool failure_chk(
	const ExecutionState& es,
	const std::string& name)
{
	if (fm[name] > FAILURE_WATERMARK) {
		std::cerr << "[PS] Failed " << fm[name] <<
			" times on " << name << '\n';
		fm[name] = 0;
		return true;
	}


	return false;
}

/* return true if backup es should proceed without psdb */
static bool failure_update(
	const ExecutionState& es,
	const std::string& name,
	bool replay_failed)
{
	uint64_t	stack_hash = es.stack.hash();

	if (replay_failed == false) {
		fm[name] = 0;
		fm2[stack_hash] = 0;
		return false;
	}

	/* increase failure count */
	fm[name] = fm[name] + 1;
	fm_total[name] = fm_total[name] + 1;
	fm2_total[stack_hash] = fm2_total[stack_hash] + 1;

	if (fm2[stack_hash] > FAILURE_STK_WATERMARK) {
		std::cerr << "[PS] Failed " << fm2[stack_hash] <<
			" times on stack for " << name << '\n';
		fm2[stack_hash] = 0;
		return true;
	}

	fm2[stack_hash] = fm2[stack_hash] + 1;
	return false;
}

static void dispatchKTest(
	Executor* exe,
	ExecutionState& es,
	KTest* kt,
	const std::string& name,
	unsigned total_tests)
{
	ForksKTest		*fork_ktest;
	ExecutionState		*backup_es;
	StateSolver		*old_ss, *new_ss;
	bool			old_cet, failed;

	if (failure_chk(es, name))
		return;

	fork_ktest = new ForksKTest(*exe);
	fork_ktest->setMakeErrTests(false);
	fork_ktest->setKTest(kt, &es);
	fork_ktest->setForkSuppress(true);
	fork_ktest->setConstraintOmit(false);

	backup_es = exe->pureFork(es, false);

	old_fork = exe->getForking();
	exe->setForking(fork_ktest);
	
	old_cet = llvm::ConcretizeEarlyTerminate;
	llvm::ConcretizeEarlyTerminate = false; 

	old_ss = exe->getSolver();
	new_ss = new KTestStateSolver(old_ss, es, kt);
	exe->setSolver(new_ss);

	WallTimer	wt;
	int		st_c_old(exe->getNumStates());
	unsigned	tq_c_old(stats::queriesTopLevel);
	unsigned	rq_c_old(StateSolver::getRealQueries());

	/* XXX: This makes the executor is reentrant. Should I be worried? */
	exe->exhaustState(&es);

	failed = (exe->getNumStates() - st_c_old) < 1;
	if (!failed) {
		/* no need for backup es, got what we needed */
		if (!es.newInsts && (rand() % total_tests) != 0)
			exe->terminate(*backup_es);
	}

	/* if failed and should try again, backup should start before
	 * call to partseed begin */
	if (!failure_update(es, name, failed) && failed) {
		backup_es->abortInstruction();
	}

	std::cerr << "[PS] "
		<< "Queries=" << StateSolver::getRealQueries() - rq_c_old
		<< ". TQs=" << stats::queriesTopLevel - tq_c_old
		<< ". CheapForks=" << fork_ktest->getCheapForks()
		<< ". Time=" << wt.checkSecs() << "s\n";
	std::cerr << "[PS] Done seeding.\n";

	llvm::ConcretizeEarlyTerminate = old_cet;
	exe->setForking(old_fork);
	exe->setSolver(old_ss);
	delete new_ss;
	delete fork_ktest;
}

SFH_DEF_ALL(PartSeedBeginReplay, "klee_partseed_begin", true)
{
	std::string		name;
	std::stringstream	ss;
	static psdb_t		psdb;
	static int		recur_depth = 0;
	unsigned		sel_idx;
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
	if (rand() % 2) {
		sel_idx = it->second[rand() % it->second.size()];
	} else {
		static unsigned c = 0;
		sel_idx = it->second[(c++) % it->second.size()];
	}
	ss << sel_idx << ".ktest.gz";
	kt = kTest_fromFile(ss.str().c_str());
	if (kt == NULL) {
		std::cerr << "[PS] Could not open ktest " << ss.str() << '\n';
		state.bindLocal(target, MK_CONST(0xdeadbeef, 64));
		return;
	}

	std::cerr << "[PS] Loaded ktest " << ss.str() << '\n';
	state.bindLocal(target, MK_CONST(sel_idx, 64));

	recur_depth++;
	dispatchKTest(sfh->executor, state, kt, name, it->second.size());
	recur_depth--;

	kTest_free(kt);
}

SFH_DEF_ALL(PartSeedEndReplay, "klee_partseed_end", false)
{
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
		std::cerr << "[PS] Replay mode enabled.\n";
		sfh->addHandler(HandlerPartSeedBeginReplay::hinfo);
		sfh->addHandler(HandlerPartSeedEndReplay::hinfo);
	} else {
		sfh->addHandler(HandlerPartSeedBeginCollect::hinfo);
		sfh->addHandler(HandlerPartSeedEndCollect::hinfo);
	}
}
}

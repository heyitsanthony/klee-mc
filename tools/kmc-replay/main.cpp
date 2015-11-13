#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include "klee/Internal/ADT/Crumbs.h"
#include "klee/Internal/ADT/KTestStream.h"
#include "klee/Internal/ADT/KTSFuzz.h"
#include "klee/Internal/Support/Watchdog.h"
#include "static/Sugar.h"
#include "SyscallsModel.h"
#include "guestptmem.h"
#include "guestmemdual.h"
#include "vexcpustate.h"

/* vexllvm stuff */
#include "ReplayExec.h"
#include "genllvm.h"
#include "guest.h"
#include "ptimgchk.h"
#include "guestptimg.h"
#include "vexexecchk.h"
#include "ChkExec.h"

/* and all the rest */
#include "UCState.h"
#include "SyscallsKTest.h"

#include <sstream>

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

using namespace klee;

#define KMC_DEFAULT_TIMEOUT	180	/* 3 minutes! */

extern void dumpIRSBs(void) {}

extern KTestStream* setupUCFunc(
	Guest		*gs,
	const char	*func,
	const char	*dirname,
	unsigned	test_num);

struct ReplayInfo
{
	const char	*dirname;
	unsigned	test_num;
	const char	*guestdir; /* = NULL */

	std::string getKTestPath(void) const {
		char		fname_ktest[256];
		snprintf(
			fname_ktest,
			256,
			"%s/test%06d.ktest.gz", dirname, test_num);
		return fname_ktest;
	}
};

static const char* get_kmc_interp(void)
{
	const char	*s;
	if ((s = getenv("KMC_INTERP_CONC"))) return s;
	if ((s = getenv("KMC_INTERP_SYM"))) return s;
	return nullptr;
}

static void loadSymArgv(Guest* gs, KTestStream* kts)
{
	/* load symbolic data into arg pointers */
	std::vector<guest_ptr>	argv;
	const KTestObject	*kto;

	argv = gs->getArgvPtrs();
	assert (argv.size() != 0);

	fprintf(stderr,
		"[kmc-replay] Restoring %d symbolic arguments\n",
		(int)(argv.size()-1));

	kto = kts->peekObject();
	if (strncmp(kto->name, "argv", 4) != 0) {
		fprintf(stderr, "[kmc-replay] symbolic argv not found..\n");
		return;
	}

	foreach (it, argv.begin()+1, argv.end()) {
		guest_ptr		p(*it);
		kto = kts->nextObject();
		assert (strncmp(kto->name, "argv", 4) == 0);
		gs->getMem()->memcpy(p, kto->bytes, kto->numBytes);
	}
}

static void loadSymArgs(Guest* gs, KTestStream* kts)
{
	guest_ptr		argcp;
	const KTestObject	*kto;

	loadSymArgv(gs, kts);
	argcp = gs->getArgcPtr();

	kto = kts->peekObject();
	if (kto == NULL) return;

	if (strncmp(kto->name, "argc", 4) != 0) return;

	kto = kts->nextObject();
	if (argcp) {
		gs->getMem()->writeNative(argcp, *((uintptr_t*)kto->bytes));
	} else {
		std::cerr << "[kmc-replay] argc obj but no argc ptr. uhoh!\n";
	}
}

static KTestStream* setupKTestStream(
	const ReplayInfo& ri,
	Guest* gs,
	std::unique_ptr<UCState> &uc_state)
{
	KTestStream	*kts;
	const char	*uc_func;
	const char	*corrupt;
	auto		fname_ktest(ri.getKTestPath());

	corrupt = getenv("KMC_CORRUPT_OBJ");
	if (corrupt == NULL)
		kts = KTestStream::create(fname_ktest.c_str());
	else {
		KTSFuzz		*ktsf;
		const char	*fuzz_percent;

		ktsf = KTSFuzz::create(fname_ktest.c_str());
		fuzz_percent = getenv("KMC_CORRUPT_PERCENT");
		ktsf->fuzzPart(
			atoi(corrupt),
			(fuzz_percent != NULL)
				? atof(fuzz_percent)/100.0
				: 0.5);
		kts = ktsf;
	}

	assert (kts != NULL && "Expects ktest");

	if (kts->getKTest()->symArgvs) loadSymArgs(gs, kts);

	uc_func = getenv("UC_FUNC");
	uc_state = nullptr;
	if (uc_func != NULL) {
		uc_state = std::unique_ptr<UCState>(UCState::init(gs, uc_func, kts));
		assert (uc_state != NULL);
	}

	return kts;
}

static void run_uc(VexExec* ve, std::unique_ptr<UCState> uc_state)
{
	const char	*uc_save;

	/* special case for UC: we're not guaranteed to exit, so we
	 * need to hook in a special return point */
	ve->beginStepping();
	while (ve->stepVSB()) {
		if (ve->getNextAddr() == 0xdeadbeef) {
			std::cerr << "[kmc-replay] UC: Exited.\n";
			if (getenv("VEXLLVM_DUMP_STATES") != 0)
				ve->getGuest()->print(std::cerr);
			break;
		}
	}

	if ((uc_save = getenv("UC_SAVE")) != nullptr)
		uc_state->save(uc_save);
}

Syscalls* getSyscalls(
	Guest*	gs,
	VexExec* ve,
	const struct ReplayInfo& ri,
	std::unique_ptr<UCState> &uc_state)
{
	KTestStream	*kts;
	const char	*model_lib;

	kts = setupKTestStream(ri, gs, uc_state);
	model_lib = getenv("KMC_MODEL");

	/* no model library given, use crumbs file */
	if (model_lib == NULL) {
		Crumbs		*crumbs;
		ReplayExec	*re;
		char		fname_crumbs[256];

		snprintf(	fname_crumbs, 256,
				"%s/test%06d.crumbs.gz", 
				ri.dirname, ri.test_num);
		crumbs = Crumbs::create(fname_crumbs);
		if (crumbs == NULL) {
			fprintf(
				stderr,
				"[kmc] No breadcrumb file at %s. Faking it.\n",
				fname_crumbs);
			crumbs = Crumbs::createEmpty();
		}
		assert (crumbs != NULL && "Expects crumbs");

		re = dynamic_cast<ReplayExec*>(ve);
		if (re != NULL)
			re->setCrumbs(crumbs);

		return SyscallsKTest::create(gs, kts, crumbs);
	}

	return new SyscallsModel(model_lib, kts, gs);
}


// Small wrapper to fork off klee-mc so it behaves like kmc-replay (sort of)
// the main idea is to launch klee-mc but don't mess with any of the files
// so it's easier to debug.
static int doInterpreterReplay(const ReplayInfo& ri)
{
	/* build arg list */
	std::vector<std::string>	argv;

	/* fixed args */
	argv.push_back("klee-mc");
	argv.push_back("-guest-type=sshot");
	argv.push_back("-mm-type=deterministic");
	argv.push_back("-show-syscalls");
	argv.push_back("-clobber-output");
	argv.push_back("-deny-sys-files"); // XXX I need a better way to pass this

	/* setup the test inputs (guests, test files, etc) */
	char dtemp[32];
	strcpy(dtemp, "/tmp/kmc-XXXXXX");
	auto dname = mkdtemp(dtemp);
	argv.push_back(std::string("-output-dir=") + dname);

	argv.push_back("-replay-ktest=" + ri.getKTestPath());
	if (getenv("KMC_INTERP_SYM")) {
		// replay using arguments..
		argv.push_back("-only-replay");
		argv.push_back("-replay-suppress-forks=false");
		argv.push_back("-dump-states-on-halt=false");
		argv.push_back("-use-rule-builder");
		argv.push_back("-rule-file=used.db");
		argv.push_back("-use-cache=false");
		argv.push_back("-use-cex-cache=false");
	}

	argv.push_back(std::string("-guest-sshot=") + ri.guestdir);

	/* look at ktest to see if symargs/symsargc are expected */
	auto kts = KTestStream::create(ri.getKTestPath().c_str());
	std::set<std::string> vars;
	while (auto kto = kts->peekObject()) {
		vars.insert(kto->name);
		kts->nextObject();
	}
	delete kts;
	if (vars.count("argv_1")) argv.push_back("-symargs");
	if (vars.count("argc_1")) argv.push_back("-symargc");

	/* use hcaches if found */
	struct stat s;
	if (stat("hcache", &s) == 0) {
		argv.push_back("-hcache-dir=hcache");
		argv.push_back("-hcache-fdir=hcache");
		argv.push_back("-hcache-pending=hcache");
		argv.push_back("-use-hash-solver");
	}

	/* user-defined args */
	std::stringstream	ss;
	std::string		arg;
	ss << get_kmc_interp();
	while (ss >> arg) argv.push_back(arg);

	/* convert to argv[] for execvp() */
	std::vector<const char*> argv_p;
	for (auto &arg : argv) argv_p.push_back(arg.c_str());
	argv_p.push_back(nullptr);

	std::cerr << "[kmc-replay] Now running";
	for (auto &arg : argv) std::cerr << ' ' << arg;
	std::cerr << '\n';

	execvp(argv_p[0], (char *const *)argv_p.data());

	std::cerr << "ERROR: Failed to execvp() klee-mc.\n";
	exit(2);
}

static int doJITReplay(const struct ReplayInfo& ri)
{
	GuestMem	*old_mem, *pt_mem = NULL, *dual_mem = NULL;
	VexExec		*ve;
	std::unique_ptr<UCState> uc_state;

	auto gs = Guest::load(ri.guestdir);
	assert (gs != NULL && "Expects a guest snapshot");

	if (getenv("KMC_PTRACE") != 0) {
		PTImgChk	*gpt;
		gpt = GuestPTImg::create<PTImgChk>(gs.release());
		assert (gpt != NULL);
		ve = VexExec::create<ChkExec, PTImgChk>(gpt);

		old_mem = gpt->getMem();

		pt_mem = new GuestPTMem(gpt, gpt->getPTArch()->getPID());
		dual_mem = new GuestMemDual(old_mem, pt_mem);
		gs.reset(gpt);
		gs->setMem(dual_mem);
	} else
		ve = VexExec::create<ReplayExec, Guest>(gs.get());

	assert (theGenLLVM);

	std::cerr << "[kmc-replay] Forcing fake vsyspage reads\n";
	theGenLLVM->setFakeSysReads();

	auto skt = std::unique_ptr<Syscalls>(getSyscalls(gs.get(), ve, ri, uc_state));
	assert (skt && "Couldn't create syscall harness");
	ve->setSyscalls(std::move(skt));

	if (dual_mem) {
		delete dual_mem;
		delete pt_mem;
		gs->setMem(old_mem);
	}

	if (uc_state != nullptr)
		run_uc(ve, std::move(uc_state));
	else
		ve->run();

	delete ve;

	return 0;
}

int main(int argc, char* argv[])
{
	struct ReplayInfo	ri;
	const char		*xchk_guest;
	int			err;
	Watchdog	wd(
		getenv("KMC_TIMEOUT") != NULL
			? atoi(getenv("KMC_TIMEOUT"))
			: KMC_DEFAULT_TIMEOUT);

	llvm::InitializeNativeTarget();

	if (argc < 2) {
		fprintf(stderr,
			"Usage: %s testnum [testdir [guestdir]]\n",
			argv[0]);
		return -1;
	}

	ri.test_num = atoi(argv[1]);
	fprintf(stderr, "Replay: test #%d\n", ri.test_num);

	ri.dirname = (argc >= 3) ? argv[2] : "klee-last";
	ri.guestdir = (argc >= 4) ? argv[3] : "guest-last";

	xchk_guest = getenv("XCHK_GUEST");
	if (xchk_guest != NULL) {
		std::cerr << "XCHK WITH GUEST: " << xchk_guest << '\n';
		setenv("KMC_REPLAY_IGNLOG", "true", 1);
		ri.guestdir = xchk_guest;
	}

	VexCPUState::registerCPUs();

	if (get_kmc_interp()) {
		err = doInterpreterReplay(ri);
	} else {
		err = doJITReplay(ri);
	}
	return err;
}

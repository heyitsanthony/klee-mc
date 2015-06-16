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

#include <stdlib.h>
#include <stdio.h>

using namespace klee;

#define KMC_DEFAULT_TIMEOUT	180	/* 3 minutes! */

extern void dumpIRSBs(void) {}

extern KTestStream* setupUCFunc(
	Guest		*gs,
	const char	*func,
	const char	*dirname,
	unsigned	test_num);


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
	const char* dirname,
	unsigned test_num,
	Guest* gs,
	std::unique_ptr<UCState> &uc_state)
{
	KTestStream	*kts;
	const char	*uc_func;
	char		fname_ktest[256];
	const char	*corrupt;

	snprintf(
		fname_ktest,
		256,
		"%s/test%06d.ktest.gz", dirname, test_num);
	corrupt = getenv("KMC_CORRUPT_OBJ");
	if (corrupt == NULL)
		kts = KTestStream::create(fname_ktest);
	else {
		KTSFuzz		*ktsf;
		const char	*fuzz_percent;

		ktsf = KTSFuzz::create(fname_ktest);
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

struct ReplayInfo
{
	const char	*dirname;
	unsigned	test_num;
	const char	*guestdir; /* = NULL */
};

Syscalls* getSyscalls(
	Guest*	gs,
	VexExec* ve,
	const struct ReplayInfo& ri,
	std::unique_ptr<UCState> &uc_state)
{
	KTestStream	*kts;
	const char	*model_lib;

	kts = setupKTestStream(ri.dirname, ri.test_num, gs, uc_state);
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


static int doReplay(const struct ReplayInfo& ri)
{
	Guest		*gs;
	GuestMem	*old_mem, *pt_mem = NULL, *dual_mem = NULL;
	VexExec		*ve;
	std::unique_ptr<UCState> uc_state;

	gs = Guest::load(ri.guestdir);
	assert (gs != NULL && "Expects a guest snapshot");

	if (getenv("KMC_PTRACE") != 0) {
		PTImgChk	*gpt;
		gpt = GuestPTImg::create<PTImgChk>(gs);
		assert (gpt != NULL);
		gs = gpt;
		ve = VexExec::create<ChkExec, PTImgChk>(gpt);

		old_mem = gpt->getMem();

		pt_mem = new GuestPTMem(gpt, gpt->getPID());
		dual_mem = new GuestMemDual(old_mem, pt_mem);
		gs->setMem(dual_mem);
	} else
		ve = VexExec::create<ReplayExec, Guest>(gs);

	assert (theGenLLVM);

	std::cerr << "[kmc-replay] Forcing fake vsyspage reads\n";
	theGenLLVM->setFakeSysReads();

	auto skt = std::unique_ptr<Syscalls>(getSyscalls(gs, ve, ri, uc_state));
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
	delete gs;

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
	ri.guestdir = (argc >= 4) ? argv[3] : NULL;

	xchk_guest = getenv("XCHK_GUEST");
	if (xchk_guest != NULL) {
		std::cerr << "XCHK WITH GUEST: " << xchk_guest << '\n';
		setenv("KMC_REPLAY_IGNLOG", "true", 1);
		ri.guestdir = xchk_guest;
	}

	err = doReplay(ri);
	return err;
}

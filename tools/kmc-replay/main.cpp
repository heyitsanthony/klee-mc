#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/ExecutionEngine/JIT.h>
#include "klee/Internal/ADT/Crumbs.h"
#include "klee/Internal/ADT/KTestStream.h"
#include "static/Sugar.h"

/* vexllvm stuff */
#include "ReplayExec.h"
#include "genllvm.h"
#include "guest.h"

/* and all the rest */
#include "SyscallsKTest.h"

#include <stdlib.h>
#include <stdio.h>

using namespace klee;

extern void dumpIRSBs(void) {}

extern KTestStream* setupUCFunc(
	Guest		*gs,
	const char	*func,
	const char	*dirname,
	unsigned	test_num);


static void loadSymArgs(Guest* gs, KTestStream* kts)
{
	/* load symbolic data into arg pointers */
	std::vector<guest_ptr>	argv;

	argv = gs->getArgvPtrs();
	assert (argv.size() != 0);

	fprintf(stderr,
		"[kmc-replay] Restoring %d symbolic arguments\n",
		(int)(argv.size()-1));

	foreach (it, argv.begin()+1, argv.end()) {
		guest_ptr		p(*it);
		const KTestObject	*kto;

		kto = kts->nextObject();
		assert (strcmp(kto->name, "argv") == 0);
		gs->getMem()->memcpy(p, kto->bytes, kto->numBytes);
	}
}

int main(int argc, char* argv[])
{
	Guest		*gs;
	ReplayExec	*re;
	SyscallsKTest	*skt;
	KTestStream	*kts;
	Crumbs		*crumbs;
	unsigned int	test_num;
	const char	*dirname;
	char		fname_crumbs[256];
	const char	*uc_func;

	llvm::InitializeNativeTarget();

	if (argc < 2) {
		fprintf(stderr, "Usage: %s testnum [directory]\n", argv[0]);
		return -1;
	}

	test_num = atoi(argv[1]);
	fprintf(stderr, "Replay: test #%d\n", test_num);

	dirname = (argc == 3) ? argv[2] : "klee-last";
	snprintf(fname_crumbs, 256, "%s/test%06d.crumbs.gz", dirname, test_num);

	gs = Guest::load();
	assert (gs != NULL && "Expects a guest snapshot");

	crumbs = Crumbs::create(fname_crumbs);

	re = VexExec::create<ReplayExec, Guest>(gs);
	assert (theGenLLVM);

	uc_func = getenv("REPLAY_UCFUNC");
	if (uc_func != NULL) {
		kts = setupUCFunc(gs, uc_func, dirname, test_num);
		if (crumbs == NULL)
			crumbs = Crumbs::createEmpty();
	} else {
		char	fname_ktest[256];
		snprintf(
			fname_ktest,
			256,
			"%s/test%06d.ktest.gz", dirname, test_num);
		kts = KTestStream::create(fname_ktest);
		if (crumbs == NULL) {
			fprintf(
				stderr, 
				"No breadcrumb file at %s\n",
				fname_crumbs);
			return -2;
		}
	}

	assert (crumbs != NULL && "Expects crumbs");
	assert (kts != NULL && "Expects ktest");
	if (kts->getKTest()->symArgvs)
		loadSymArgs(gs, kts);

	std::cerr << "[kmc-replay] Forcing fake vsyspage reads\n";
	theGenLLVM->setFakeSysReads();

	re->setCrumbs(crumbs);

	skt = SyscallsKTest::create(gs, kts, crumbs);
	assert (skt != NULL && "Couldn't create ktest harness");

	re->setSyscallsKTest(skt);

	if (uc_func == NULL) {
		re->run();
	} else {
		/* special case for UC: we're not guaranteed to exit, so we
		 * need to hook in a special return point */
		re->beginStepping();
		while (re->stepVSB()) {
			if (re->getNextAddr() == 0xdeadbeef)
				break;
		}
	}

	delete re;
	delete gs;

	return 0;
}

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include "llvm/Target/TargetSelect.h"
#include "llvm/ExecutionEngine/JIT.h"

#include "ReplayExec.h"
#include "guest.h"

#include "SyscallsKTest.h"
#include <stdlib.h>
#include <stdio.h>

extern void dumpIRSBs(void) {}

int main(int argc, char* argv[])
{
	Guest		*gs;
	ReplayExec	*re;
	SyscallsKTest	*skt;
	unsigned int	test_num;
	const char	*dirname;
	char		fname_ktest[256];
	char		fname_reglog[256];

	llvm::InitializeNativeTarget();

	if (argc < 2) {
		fprintf(stderr, "Usage: %s testnum [directory]\n", argv[0]);
		return -1;
	}

	test_num = atoi(argv[1]);
	fprintf(stderr, "Replay: test #%d\n", test_num);

	dirname = (argc == 3) ? argv[2] : "klee-last";
	snprintf(fname_ktest, 256, "%s/test%06d.ktest.gz", dirname, test_num);
	snprintf(fname_reglog, 256, "%s/test%06d.reglog", dirname, test_num);

	gs = Guest::load();
	assert (gs != NULL && "Expects a guest snapshot");

	re = VexExec::create<ReplayExec, Guest>(gs);
	skt = SyscallsKTest::create(gs, fname_ktest);
	assert (skt != NULL && "Couldn't create ktest harness");

	re->setSyscallsKTest(skt);
	re->setRegLog(fname_reglog);
	re->run();
	
	delete re;
	delete gs;

	return 0;
}

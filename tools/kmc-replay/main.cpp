#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include "llvm/Target/TargetSelect.h"
#include "llvm/ExecutionEngine/JIT.h"

#include "vexexec.h"
#include "guest.h"

#include "SyscallsKTest.h"

#include <stdio.h>

extern void dumpIRSBs(void) {}

class ReplayExec : public VexExec
{
public:
	virtual ~ReplayExec(){ }

	void setSyscallsKTest(SyscallsKTest* in_skt)
	{
		delete sc;
		skt = in_skt;
		sc = skt;
	}

	ReplayExec(Guest* gs, VexXlate* vx = NULL)
	: VexExec(gs, vx),
	  skt(NULL)
	{ }
private:
	SyscallsKTest	*skt;
};

int main(int argc, char* argv[])
{
	Guest		*gs;
	ReplayExec	*re;
	SyscallsKTest	*skt;
	const char	*fname_ktest, *fname_sclog;

	llvm::InitializeNativeTarget();

	if (argc != 3) {
		fprintf(stderr, "Usage: %s fname_ktest fname_sclog\n",
			argv[0]);
		return -1;
	}

	fname_ktest = argv[1];
	fname_sclog = argv[2];
	gs = Guest::load();
	assert (gs != NULL && "Expects a guest snapshot");

	re = VexExec::create<ReplayExec, Guest>(gs);
	skt = SyscallsKTest::create(gs, fname_ktest, fname_sclog);
	assert (skt != NULL && "Couldn't create ktest harness");

	re->setSyscallsKTest(skt);
	re->run();
	
	delete re;
	delete gs;

	return 0;
}

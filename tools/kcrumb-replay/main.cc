#include <assert.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "ptimgremote.h"
#include "klee/Internal/ADT/KTestStream.h"
#include "klee/Internal/ADT/KTSFuzz.h"
#include "klee/Internal/ADT/Crumbs.h"
#include "klee/Internal/Support/Watchdog.h"
#include "SyscallKTestBC.h"
#include "guestcpustate.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MAX_ARGS	128
#define KCRUMB_DEFAULT_TIMEOUT	180	/* 3 minutes! */

using namespace klee;
extern void dumpIRSBs(void) {}

static char** loadSymArgs(KTestStream* kts)
{
	char			**ret;
	unsigned		i;
	const KTestObject	*kto;

	ret = (char**)malloc(MAX_ARGS * sizeof(char**)/* (exe,[args],NULL) */);

	fprintf(stderr, "[kcrumb-replay] Restoring symbolic arguments\n");

	/* load argv */
	ret[0] = NULL; /* program name filled in later */
	i = 1;
	do {
		kto = kts->peekObject();

		if (kto == NULL) break;
		if (strncmp(kto->name, "argv", 4) != 0)
			break;
		ret[i] = (char*)malloc(kto->numBytes+1);
		memcpy(ret[i], kto->bytes, kto->numBytes);
		ret[i][kto->numBytes] = '\0';

		kts->nextObject();
		i++;
	} while(1);

	ret[i] = NULL;

	/* enforce argc */
	kto = kts->peekObject();
	if (strncmp(kto->name, "argc", 4) == 0) {
		uint64_t v;
		memcpy(&v, kto->bytes, sizeof(v));
		kts->nextObject();
		ret[v] = NULL;
	}

	return ret;
}


/* add valgrind check */
static void setup_prefix(char* args[], int arg_len)
{
	memmove(&args[2], &args[0], sizeof(char*)*(arg_len - 3));
	args[0] = strdup("/usr/bin/valgrind");
	args[1] = strdup("--suppressions=/dev/null");
}

int main(int argc, char *argv[], char* envp[])
{
	const char	*sshot_path, *test_path;
	int		ok, status;
	int		test_num;
	pid_t		pid;
	KTestStream	*kts;
	PTImgRemote	*ptimg;
	SyscallsKTestBC	*sc;
	char		**args;
	Watchdog	wd(
		getenv("KMC_TIMEOUT") != NULL
			? atoi(getenv("KMC_TIMEOUT"))
			: KCRUMB_DEFAULT_TIMEOUT);

	if (argc <= 3) {
		fprintf(stderr, "Usage: %s sshot_path test_num execfile\n",
			argv[0]);
		return -1;
	}

	sshot_path = argv[1];
	test_num = atoi(argv[2]);
	test_path = getenv("KMC_TEST_PATH");
	if (test_path == NULL) test_path = "klee-last";

	kts = SyscallsKTestBC::createKTS(test_path, test_num);
	if (kts->getKTest()->symArgvs) {
		args = loadSymArgs(kts);
		args[0] = argv[3];
	} else {
		assert (0 == 1 && "XXX WRONG");
		args = &argv[3];
	}

	setup_prefix(args, 126);


	ptimg = GuestPTImg::create<PTImgRemote>(
		(argc - 3) + 1, /* ignore (self, sshot_path, test_num) */
		args,
		envp);
	pid = ptimg->getPID();

	sc = SyscallsKTestBC::create(ptimg, kts, test_path, test_num);
	status = (SIGTRAP << 8) | 0x7f; /* XXX junk */

	ptrace(PTRACE_SYSCALL, pid, 0, 0);

	/* we are left at the entrance to a system call... */
	bool	is_enter = true;
	while (waitpid(pid, &status, 0) == pid) {
		int	sig;

		if (!WIFSTOPPED(status))
			break;

		sig = WSTOPSIG(status);

		if (sig == SIGTRAP) {
			bool	applied = false;
			if (is_enter)
				applied = sc->apply();
			if (applied)
				is_enter = true;
			else
				is_enter = !is_enter;
			sig = 0; /* eat signal */
		}

		if (sig == SIGKILL || sig == SIGSTOP) {
			std::cerr << "{SIGKILL,SIGSTOP} detected. Dumping\n";
			ptimg->slurpRegisters(ptimg->getPID());
			ptimg->getCPUState()->print(std::cerr);
			break;
		}

#if 0
		if (sig == SIGSEGV) {
			std::cerr << "SIGSEGV detected. Dumping\n";
			ptimg->slurpRegisters(ptimg->getPID());
			ptimg->getCPUState()->print(std::cerr);
		}
#endif

		ok = ptrace(PTRACE_SYSCALL, pid, 0, sig);
	}

	if (WIFEXITED(status))
		printf("exited status=%d\n", WEXITSTATUS(status));
	else
		printf("bad status=%d\n", status);

	delete sc;
	/* deleting ptremoteimgs is broken because of ptrlists and
	 * procmap ew */
	// delete ptimg;

	return 0;
}

/* -*- mode: c++; c-basic-offset: 2; -*- */

#include "klee/Common.h"
#include "klee/Interpreter.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/Statistics.h"
#include "klee/Internal/System/Time.h"
#include "klee/Internal/ADT/TwoOStreams.h"
#include "static/Sugar.h"
#include "../../lib/Skins/GDBExecutor.h"
#include "../../lib/Skins/ShadowExecutor.h"
#include "../../lib/Skins/TaintMergeExecutor.h"
#include "../../lib/Skins/ConstraintSeedExecutor.h"
#include "../../lib/Skins/DDTExecutor.h"
#include "KleeHandlerVex.h"
#include "UCHandler.h"
#include "klee/Internal/ADT/CmdArgs.h"

#include "guest.h"
#include "guestptimg.h"
#include "guestfragment.h"
#include "elfimg.h"
#include "guestelf.h"
#include "ExecutorVex.h"
#include "ExeSymHook.h"
#include "ExeChk.h"
#include "ExeUC.h"

// FIXME: Ugh, this is gross. But otherwise our config.h conflicts with LLVMs.
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/Signals.h>
#include <iostream>
#include <fstream>
#include <time.h>
#include <pthread.h>

#include <iostream>
#include <fstream>
#include <sstream>

using namespace llvm;
using namespace klee;

bool			SymArgs;
static pthread_t	pt_watchdog;
extern double		MaxSTPTime;
extern bool		WriteTraces;
extern double		MaxTime;

namespace {
	cl::opt<bool, true> SymArgsProxy(
		"symargs",
		cl::desc("Make argument strings symbolic"),
		cl::location(SymArgs),
		cl::init(false));

	cl::opt<bool>
	UseConstraintSeed("use-constr-seed", cl::desc("Use constraint seeds."));

	cl::opt<bool> UseDDT("use-ddt", cl::desc("Data dependency tainting."));

	cl::opt<bool> UseTaint(
		"use-taint",
		cl::desc("Taint functions or something. EXPERIMENTAL"));

	cl::opt<bool> UseTaintMerge(
		"use-taint-merge",
		cl::desc("Taint and merge specific functions."));

	cl::opt<bool> SymHook(
		"use-symhooks",
		cl::desc("Apply additional analysis by tracking library calls"));

	cl::opt<bool> UseGDB("use-gdb", cl::desc("Enable remote GDB monitoring"));

	cl::opt<std::string>
	InputFile(
		cl::desc("<input executable>"),
		cl::Positional,
		cl::init("-"));

	cl::opt<std::string>
	Environ("environ",
		cl::desc("Parse environ from given file (in \"env\" format)"));

	cl::list<std::string>
	InputArgv(
		cl::ConsumeAfter,
		cl::desc("<program arguments>..."));

	// this is a fake entry, its automagically handled
	cl::list<std::string>
	ReadArgsFilesFake(
		"read-args",
		cl::desc("File to read arguments from (one arg per line)"));

	cl::opt<bool>
	ReplayKeepSymbolic(
		"replay-keep-symbolic",
		cl::desc("Replay the test cases only by asserting"
		"the bytes, not necessarily making them concrete."));

	cl::list<std::string>
	ReplayKTestFile(
		"replay-ktest",
		cl::desc("Specify a ktest file to replay"),
		cl::value_desc("ktest file"));

	cl::list<std::string>
	ReplayKTestDir(
		"replay-ktest-dir",
		cl::desc("Specify a directory to replay .ktest files from"),
		cl::value_desc("ktest directory"));

	cl::opt<std::string>
	ReplayPathFile(
		"replay-path",
		cl::desc("Specify a path file to replay"),
		cl::value_desc("path file"));

	cl::opt<std::string>
	ReplayPathDir(
		"replay-path-dir",
		cl::desc("Specify a directory to replay path files from"),
		cl::value_desc("path directory"));

	cl::list<std::string> SeedOutFile("seed-out");
	cl::list<std::string> SeedOutDir("seed-out-dir");

	cl::opt<std::string>
	GuestType(
		"guest-type",
		cl::desc("Type of guest to use. {*ptrace, sshot, elf, frag}"),
		cl::init("ptrace"));

	cl::opt<std::string>
	GuestSnapshotFName(
		"guest-sshot",
		cl::desc("Snapshot file to use."),
		cl::init(""));

	cl::opt<std::string>
	GuestFragmentFile(
		"guestfrag-file",
		cl::desc("File containing fragment data."),
		cl::init("default.frag"));

	cl::opt<unsigned>
	GuestFragmentBase(
		"guestfrag-base",
		cl::desc("Base of the fragment"),
		cl::init(0x400000));

	cl::opt<bool>
	Unconstrained("unconstrained", cl::desc("Unconstrained Execution."));

	cl::opt<bool>
	XChkJIT("xchkjit",
		cl::desc("Cross check concrete / no syscall binary with JIT."));

	cl::opt<bool>
	Watchdog("watchdog",
		cl::desc("Use a watchdog thread to enforce --max-time."));

}

static Interpreter *theInterpreter = 0;
static bool interrupted = false;

static void parseArguments(int argc, char **argv)
{
  std::vector<std::string> arguments;

  for (int i=1; i<argc; i++) {
    if (!strcmp(argv[i],"--read-args") && i+1<argc) {
      CmdArgs::readArgumentsFromFile(argv[++i], arguments);
    } else {
      arguments.push_back(argv[i]);
    }
  }

  int numArgs = arguments.size() + 1;
  const char **argArray = new const char*[numArgs+1];
  argArray[0] = argv[0];
  argArray[numArgs] = 0;
  for (int i=1; i<numArgs; i++) {
    argArray[i] = arguments[i-1].c_str();
  }

  cl::ParseCommandLineOptions(numArgs, const_cast<char**>(argArray)," klee\n");
  delete[] argArray;
}

// Pulled out so it can be easily called from a debugger.
extern "C" void halt_execution() { theInterpreter->setHaltExecution(true); }
extern "C" void stop_forking() { theInterpreter->setInhibitForking(true); }

static void interrupt_handle()
{
	if (interrupted || !theInterpreter) {
		std::cerr << "KLEE: ctrl-c detected, exiting.\n";
		exit(1);
	}

	std::cerr << "KLEE: ctrl-c detected, requesting interpreter to halt.\n";
	halt_execution();
	sys::SetInterruptFunction(interrupt_handle);
	interrupted = true;
}

void dumpIRSBs(void)
{
	std::cerr << "DUMPING LOGS" << std::endl;
	std::cerr << "(NO DUMPING IN TEST JIT)" << std::endl;
}

void run(ExecutorVex* exe) { exe->runImage(); }

void runReplayKTest(Interpreter* interpreter)
{
	std::vector<KTest*>		kTests;
	std::vector<std::string>	outFiles;
	unsigned			i=0;

	outFiles = ReplayKTestFile;
	foreach (it, ReplayKTestDir.begin(),  ReplayKTestDir.end())
		KleeHandler::getOutFiles(*it, outFiles);

	foreach (it, outFiles.begin(), outFiles.end()) {
		KTest *out = kTest_fromFile(it->c_str());
		if (out) {
			kTests.push_back(out);
		} else {
			std::cerr << "KLEE: unable to open: " << *it << "\n";
		}
	}

	foreach (it, kTests.begin(), kTests.end()) {
		KTest *out = *it;

		interpreter->setReplayKTest(out);
		std::cerr
			<< "KLEE: replaying: " << *it << " ("
			<< kTest_numBytes(out) << " bytes)"
			<< " (" << ++i << "/" << outFiles.size() << ")\n";

		// XXX should put envp in .ktest ?
		run(dynamic_cast<ExecutorVex*>(interpreter));
		if (interrupted)
			break;
	}

	interpreter->setReplayKTest(0);

	while (!kTests.empty()) {
		kTest_free(kTests.back());
		kTests.pop_back();
	}
}

#define GET_STAT(x,y)	\
	uint64_t x = *theStatisticManager->getStatisticByName(y);

static void printStats(PrefixWriter& info, KleeHandler* handler)
{
	GET_STAT(queries, "Queries")
	GET_STAT(queriesValid, "QueriesValid")
	GET_STAT(queriesInvalid, "QueriesInvalid")
	GET_STAT(queryCounterexamples, "QueriesCEX")
	GET_STAT(queriesFailed, "QueriesFailed")
	GET_STAT(queryConstructs, "QueriesConstructs")
	GET_STAT(queryCacheHits, "QueryCacheHits")
	GET_STAT(queryCacheMisses, "QueryCacheMisses")
	GET_STAT(instructions, "Instructions")
	GET_STAT(forks, "Forks")

	info << "done: total queries = "
		<< queries << " ("
		<< "valid: " << queriesValid << ", "
		<< "invalid: " << queriesInvalid << ", "
		<< "cex: " << queryCounterexamples << ", "
		<< "failed: " << queriesFailed << ")\n";
	if (queries)
		info	<< "done: avg. constructs per query = "
			<< queryConstructs / queries << "\n";

	info	<< "done: query cache hits = " << queryCacheHits << ", "
		<< "query cache misses = " << queryCacheMisses << "\n";

	info << "done: total instructions = " << instructions << "\n";
	info << "done: explored paths = " << 1 + forks << "\n";
	info << "done: completed paths = " << handler->getNumPathsExplored() << "\n";
	info << "done: generated tests = " << handler->getNumTestCases() << "\n";
}

static CmdArgs* getCmdArgs(char** envp)
{
	std::vector<std::string>	input_args;
	CmdArgs				*ret;
	const std::string		env_path(Environ);

	foreach (it, InputArgv.begin(), InputArgv.end())
		input_args.push_back(*it);

	ret = new CmdArgs(InputFile, env_path, envp, input_args);
	if (SymArgs)
		ret->setSymbolic();
	return ret;
}

Guest* getGuest(CmdArgs* cmdargs)
{
	Guest	*gs = NULL;

	if (GuestType == "ptrace") {
		gs = GuestPTImg::create<GuestPTImg>(
			cmdargs->getArgc(),
			cmdargs->getArgv(),
			cmdargs->getEnvp());
	} else if (GuestType == "elf") {
		ElfImg		*img;
		GuestELF	*ge;

		img = ElfImg::create(cmdargs->getArgv()[0]);
		if (img == NULL) {
			fprintf(stderr, "Could not open ELF\n");
			return NULL;
		}

		ge = new GuestELF(img);
		ge->setArgv(
			cmdargs->getArgc(),
			const_cast<const char**>(cmdargs->getArgv()),
			(int)cmdargs->getEnvc(),
			const_cast<const char**>(cmdargs->getEnvp()));

		gs = ge;
	} else if (GuestType == "sshot") {
		fprintf(stderr, "[klee-mc] LOADING SNAPSHOT\n");
		gs = Guest::load(
			GuestSnapshotFName.size() == 0
			?	NULL
			:	GuestSnapshotFName.c_str());
		assert (gs && "Could not load guest snapshot");

		/* ugh, got to force load so replay knows how many symargs
		 * we have */
		if (cmdargs->isSymbolic()) {
			std::vector<guest_ptr>		ptrs(gs->getArgvPtrs());
			std::vector<std::string>	arg_l;

			foreach (it, ptrs.begin(), ptrs.end()) {
				const char	*s;
				s =  (const char*)gs->getMem()->getHostPtr(*it);
				arg_l.push_back(std::string(s));
			}

			cmdargs->setArgs(arg_l);
		}
	} else if (GuestType == "frag") {
		GuestFragment	*gf;

		gf = GuestFragment::fromFile(
			GuestFragmentFile.c_str(),
			Arch::X86_64,
			guest_ptr(GuestFragmentBase));
		gs = gf;
	} else {
		assert ("unknown guest type");
	}

	return gs;
}

static bool isReplaying(void)
{ return (!ReplayKTestDir.empty() || !ReplayKTestFile.empty()); }

#define NEW_INTERP(x)					\
	((UseGDB)					\
		? new GDBExecutor<x>(handler) 		\
	: (UseConstraintSeed)				\
		? new ConstraintSeedExecutor<x>(handler)\
	: (UseDDT)					\
		? new DDTExecutor<x>(handler)		\
	: (UseTaintMerge)				\
		? new TaintMergeExecutor<x>(handler)	\
	: (UseTaint)					\
		? new ShadowExecutor<x>(handler)	\
	: new x(handler))

Interpreter* createInterpreter(KleeHandler *handler, Guest* gs)
{
	if (Unconstrained) return NEW_INTERP(ExeUC);
	if (SymHook) return NEW_INTERP(ExeSymHook);
	if (XChkJIT) return NEW_INTERP(ExeChk);

	return NEW_INTERP(ExecutorVex);
}

static std::list<ReplayPath>	replayPaths;
void setupReplayPaths(Interpreter* interpreter)
{
	std::vector<std::string>	pathFiles;

	if (ReplayPathDir != "")
		KleeHandler::getPathFiles(ReplayPathDir, pathFiles);
	if (ReplayPathFile != "")
		pathFiles.push_back(ReplayPathFile);

	KleeHandler::loadPathFiles(pathFiles, replayPaths);

	if (replayPaths.empty()) return;

	interpreter->setReplayPaths(&replayPaths);
}

static void* watchdog_thread(void* x)
{
	struct timespec		ts, rem;
	int			rc;
	ssize_t			sz;

	/* don't expect any one to try to join with watchdog  */
	pthread_detach(pthread_self());

	ts.tv_sec = MaxTime;
	ts.tv_nsec = 0;
	do {
		rc = nanosleep(&ts, &rem);
		ts = rem;
	} while (rc == -1);

	sz = write(2, "watchdog timeout\n", 17);

	/* the _exit() is important because this is a signal,
	 * otherwise we segfault! */
	_exit(-1);

	return NULL;
}

int main(int argc, char **argv, char **envp)
{
	KleeHandler	*handler;
	CmdArgs		*cmdargs;
	Guest		*gs;
	Interpreter	*interpreter;

	fprintf(stderr, "[klee-mc] git-commit: " GIT_COMMIT "\n");
	atexit(llvm_shutdown);

	llvm::InitializeNativeTarget();

	parseArguments(argc, argv);
	sys::PrintStackTraceOnErrorSignal();

	sys::SetInterruptFunction(interrupt_handle);

	if (Watchdog && MaxTime)
		pthread_create(&pt_watchdog, NULL, watchdog_thread, NULL);

	cmdargs = getCmdArgs(envp);
	gs = getGuest(cmdargs);
	if (gs == NULL) {
		fprintf(stderr, "[klee-mc] Could not get guest.\n");
		return 2;
	}

	if (!Unconstrained) {
		handler = new KleeHandlerVex(cmdargs, gs);
	} else {
		handler = new UCHandler(cmdargs, gs);
	}

	interpreter = createInterpreter(handler, gs);

	theInterpreter = interpreter;
	handler->setInterpreter(interpreter);

	std::ostream &infoFile = handler->getInfoStream();
	for (int i=0; i < argc; i++) {
		infoFile << argv[i] << (i+1<argc ? " ":"\n");
	}

	TwoOStreams info2s(&std::cerr, &infoFile);
	PrefixWriter info(info2s, "KLEE: ");
	info << "PID: " << getpid() << "\n";

	setupReplayPaths(interpreter);
	if (isReplaying()) {
		runReplayKTest(interpreter);
	} else
		run(dynamic_cast<ExecutorVex*>(interpreter));

	delete interpreter;
	delete gs;

	printStats(info, handler);
	delete handler;

	delete cmdargs;

	llvm_shutdown();

	return 0;
}

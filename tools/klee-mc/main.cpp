/* -*- mode: c++; c-basic-offset: 2; -*- */

#include "klee/Common.h"
#include "klee/Interpreter.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/Statistics.h"
#include "klee/Internal/System/Time.h"
#include "klee/Internal/ADT/TwoOStreams.h"
#include "static/Sugar.h"

#include "KleeHandler.h"
#include "cmdargs.h"

#include <llvm/Support/CommandLine.h>
#include <llvm/Support/ManagedStatic.h>

#include "guest.h"
#include "guestptimg.h"
#include "guestfragment.h"
#include "elfimg.h"
#include "guestelf.h"
#include "ExecutorVex.h"
#include "ExeSymHook.h"
#include "ExeChk.h"

//#include "llvm/Support/system_error.h"

// FIXME: Ugh, this is gross. But otherwise our config.h conflicts with LLVMs.
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/Signals.h>
#include <iostream>
#include <fstream>
#include <cerrno>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/times.h>
#include <signal.h>

#include <iostream>
#include <iterator>
#include <fstream>
#include <sstream>

using namespace llvm;
using namespace klee;

bool		SymArgs;
extern double	MaxSTPTime;
extern bool	WriteTraces;
extern double	MaxTime;

namespace {
	cl::opt<bool, true> SymArgsProxy(
		"symargs",
		cl::desc("Make argument strings symbolic"),
		cl::location(SymArgs),
		cl::init(false));

	cl::opt<bool> SymHook(
		"use-symhooks",
		cl::desc("Apply additional analysis by tracking library calls"),
		cl::init(false));

	cl::opt<std::string>
	InputFile(
		cl::desc("<input executable>"),
		cl::Positional,
		cl::init("-"));

	cl::opt<std::string>
	RunInDir(
		"run-in",
		cl::desc("Change to the given directory prior to executing"));

	cl::opt<std::string>
	Environ(
		"environ",
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
	ReplayOutFile(
		"replay-out",
		cl::desc("Specify an out file to replay"),
		cl::value_desc("out file"));

	cl::list<std::string>
	ReplayOutDir(
		"replay-out-dir",
		cl::desc("Specify a directory to replay .out files from"),
		cl::value_desc("out directory"));

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

	cl::list<std::string>
	SeedOutFile("seed-out");

	cl::list<std::string>
	SeedOutDir("seed-out-dir");

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
	XChkJIT(
		"xchkjit",
		cl::desc("Cross check concrete / no syscall binary with JIT."),
		cl::init(false));

	cl::opt<bool>
	Watchdog(
		"watchdog",
		cl::desc("Use a watchdog process to enforce --max-time."),
		cl::init(0));

}

static Interpreter *theInterpreter = 0;
static bool interrupted = false;

std::string stripEdgeSpaces(std::string &in)
{
  unsigned len = in.size();
  unsigned lead = 0, trail = len;
  while (lead<len && isspace(in[lead]))
    ++lead;
  while (trail>lead && isspace(in[trail-1]))
    --trail;
  return in.substr(lead, trail-lead);
}

static void readArgumentsFromFile(char *file, std::vector<std::string> &results)
{
  std::ifstream f(file);
  assert(f.is_open() && "unable to open input for reading arguments");
  while (!f.eof()) {
    std::string line;
    std::getline(f, line);
    line = stripEdgeSpaces(line);
    if (!line.empty())
      results.push_back(line);
  }
  f.close();
}

static void parseArguments(int argc, char **argv)
{
  std::vector<std::string> arguments;

  for (int i=1; i<argc; i++) {
    if (!strcmp(argv[i],"--read-args") && i+1<argc) {
      readArgumentsFromFile(argv[++i], arguments);
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

static void interrupt_handle() {
  if (!interrupted && theInterpreter) {
    std::cerr << "KLEE: ctrl-c detected, requesting interpreter to halt.\n";
    halt_execution();
    sys::SetInterruptFunction(interrupt_handle);
  } else {
    std::cerr << "KLEE: ctrl-c detected, exiting.\n";
    exit(1);
  }
  interrupted = true;
}

// This is a temporary hack. If the running process has access to
// externals then it can disable interrupts, which screws up the
// normal "nice" watchdog termination process. We try to request the
// interpreter to halt using this mechanism as a last resort to save
// the state data before going ahead and killing it.
static void halt_via_gdb(int pid)
{
  char buffer[256];
  sprintf(buffer,
          "gdb --batch --eval-command=\"p halt_execution()\" "
          "--eval-command=detach --pid=%d &> /dev/null",
          pid);
  //  fprintf(stderr, "KLEE: WATCHDOG: running: %s\n", buffer);
  if (system(buffer)==-1)
    perror("system");
}

void dumpIRSBs(void)
{
	std::cerr << "DUMPING LOGS" << std::endl;
	std::cerr << "(NO DUMPING IN TEST JIT)" << std::endl;
}

void run(ExecutorVex* exe)
{
	if (RunInDir != "") {
		int res = chdir(RunInDir.c_str());
		if (res < 0) {
			klee_error(
				"Unable to change directory to: %s",
				RunInDir.c_str());
		}
	}

	exe->runImage();
}

void runReplay(Interpreter* interpreter)
{
  std::vector<KTest*> kTests;
  std::vector<std::string> outFiles;

  outFiles = ReplayOutFile;
  foreach (it, ReplayOutDir.begin(),  ReplayOutDir.end())
    KleeHandler::getOutFiles(*it, outFiles);

  foreach (it, outFiles.begin(), outFiles.end()) {
    KTest *out = kTest_fromFile(it->c_str());
    if (out) {
      kTests.push_back(out);
    } else {
      std::cerr << "KLEE: unable to open: " << *it << "\n";
    }
  }

  unsigned i=0;
  foreach (it, kTests.begin(), kTests.end()) {
    KTest *out = *it;
    interpreter->setReplayOut(out);
    std::cerr << "KLEE: replaying: " << *it << " ("
    	<< kTest_numBytes(out) << " bytes)"
        << " (" << ++i << "/" << outFiles.size() << ")\n";
    // XXX should put envp in .ktest ?
    run(dynamic_cast<ExecutorVex*>(interpreter));
    if (interrupted) break;
  }

  interpreter->setReplayOut(0);

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
	std::list<std::string>	input_args;
	CmdArgs			*ret;
	const std::string	env_path(Environ);

	foreach (it, InputArgv.begin(), InputArgv.end())
		input_args.push_back(*it);

	ret = new CmdArgs(InputFile, env_path, envp, input_args);
	if (SymArgs)
		ret->setSymbolic();
	return ret;
}

static void watchdog_alarm(int sig)
{
	ssize_t	sz;
	sz = write(2, "watchdog\n", 9);
	exit(-1);
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

bool isReplaying(void)
{
	return (!ReplayOutDir.empty() || !ReplayOutFile.empty());
}

static std::list<ReplayPathType>	replayPaths;
void setupReplayPaths(Interpreter* interpreter)
{
	ReplayPathType			replayPath;
	std::vector<std::string>	pathFiles;

	if (ReplayPathDir != "")
		KleeHandler::getPathFiles(ReplayPathDir, pathFiles);
	if (ReplayPathFile != "")
		pathFiles.push_back(ReplayPathFile);

	foreach (it, pathFiles.begin(), pathFiles.end()) {
		KleeHandler::loadPathFile(*it, replayPath);
		replayPaths.push_back(replayPath);
		replayPath.clear();
	}

	if (!replayPaths.empty()) {
		interpreter->setReplayPaths(&replayPaths);
	}
}

int main(int argc, char **argv, char **envp)
{
	KleeHandler	*handler;
	CmdArgs		*cmdargs;
	Guest		*gs;
	Interpreter	*interpreter;

	atexit(llvm_shutdown);

	llvm::InitializeNativeTarget();

	parseArguments(argc, argv);
	sys::PrintStackTraceOnErrorSignal();

	sys::SetInterruptFunction(interrupt_handle);

	if (Watchdog) {
		/* XXX, make sure no one else is using alarm() */
		MaxSTPTime = -1.0;
		signal(SIGALRM, watchdog_alarm);
		alarm((unsigned int)MaxTime);
	}

	cmdargs = getCmdArgs(envp);

	handler = new KleeHandler(cmdargs);

	gs = getGuest(cmdargs);
	if (gs == NULL) {
		fprintf(stderr, "[klee-mc] Could not get guest.\n");
		return 2;
	}

	interpreter = NULL;
	if (SymHook) {
		interpreter = ExeSymHook::create(handler, gs);
		if (interpreter == NULL) {
			fprintf(stderr,
				"Failed to create SymHook. Missing malloc?\n");
		}
	}

	if (interpreter == NULL) {
		interpreter = (XChkJIT)
			? new ExeChk(handler, gs)
			: new ExecutorVex(handler, gs);
	}

	theInterpreter = interpreter;
	handler->setInterpreter(interpreter);

	std::ostream &infoFile = handler->getInfoStream();
	for (int i=0; i < argc; i++) {
		infoFile << argv[i] << (i+1<argc ? " ":"\n");
	}

	TwoOStreams info2s(&std::cerr, &infoFile);
	PrefixWriter info(info2s, "KLEE: ");
	info << "PID: " << getpid() << "\n";

	//  finalModule = interpreter->setModule(mainModule, Opts);
	//  externalsAndGlobalsCheck(finalModule);
	setupReplayPaths(interpreter);
	if (isReplaying()) {
		runReplay(interpreter);
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

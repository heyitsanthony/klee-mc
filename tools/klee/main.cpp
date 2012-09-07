/* -*- mode: c++; c-basic-offset: 2; -*- */

#include "../../lib/Skins/SeedExecutor.h"
#include "../../lib/Core/ExecutorBC.h"
#include "klee/Common.h"
#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Interpreter.h"
#include "klee/Statistics.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/Internal/ADT/TreeStream.h"
#include "klee/Internal/ADT/TwoOStreams.h"
#include "klee/KleeHandler.h"
#include "klee/Internal/ADT/CmdArgs.h"
#include "klee/Internal/System/Time.h"
#include "static/Sugar.h"

#include "libc.h"

#include "llvm/Constants.h"
#include "llvm/Module.h"
#include "llvm/Type.h"
#include "llvm/InstrTypes.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"

//#include "llvm/Support/system_error.h"

// FIXME: Ugh, this is gross. But otherwise our config.h conflicts with LLVMs.
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include <llvm/Support/TargetSelect.h>
#include "llvm/Support/Signals.h"
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

extern void externalsAndGlobalsCheck(const Module *m);
extern Interpreter::ModuleOptions getMainModule(Module* &m);

namespace {
  cl::opt<std::string>
  InputFile(cl::desc("<input bytecode>"), cl::Positional, cl::init("-"));

  cl::opt<std::string>
  RunInDir("run-in", cl::desc("Change to the given directory prior to executing"));

  cl::opt<std::string>
  Environ("environ", cl::desc("Parse environ from given file (in \"env\" format)"));

  cl::list<std::string>
  InputArgv(cl::ConsumeAfter, cl::desc("<program arguments>..."));

  cl::opt<LibcType> Libc(
    "libc",
    cl::desc("Choose libc version (none by default)."),
    cl::values(
      clEnumValN(NoLibc, "none", "Don't link in a libc"),
      clEnumValN(KleeLibc, "klee", "Link in klee libc"),
	  	clEnumValN(UcLibc, "uclibc", "Link in uclibc (adapted for klee)"),
  		clEnumValEnd),
    cl::init(NoLibc));


  cl::opt<bool>
  WithPOSIXRuntime("posix-runtime", cl::desc("Link with POSIX runtime"));

  // this is a fake entry, its automagically handled
  cl::list<std::string>
  ReadArgsFilesFake("read-args",
                    cl::desc("File to read arguments from (one arg per line)"));

  cl::opt<bool>
  ReplayKeepSymbolic("replay-keep-symbolic",
                     cl::desc("Replay the test cases only by asserting"
                              "the bytes, not necessarily making them concrete."));

  cl::list<std::string>
  ReplayKTestFile("replay-ktest",
                cl::desc("Specify a ktest file to replay"),
                cl::value_desc("ktest file"));

  cl::list<std::string>
  ReplayKTestDir("replay-ktest-dir",
	       cl::desc("Specify a directory for replaying .ktest files"),
	       cl::value_desc("ktestdirectory"));

  cl::opt<std::string>
  ReplayPathFile("replay-path",
                 cl::desc("Specify a path file to replay"),
                 cl::value_desc("path file"));

  cl::opt<std::string>
  ReplayPathDir("replay-path-dir",
          cl::desc("Specify a directory to replay path files from"),
          cl::value_desc("path directory"));

  cl::list<std::string> SeedOutFile("seed-out");

  cl::list<std::string> SeedOutDir("seed-out-dir");

  cl::opt<bool> Watchdog(
  	"watchdog", cl::desc("Use a watchdog process to enforce --max-time."));
}

extern bool WriteTraces;
extern cl::opt<double> MaxTime;
std::string g_InputFile = InputFile;
LibcType g_Libc = Libc;
bool g_WithPOSIXRuntime;

#if ENABLE_STPLOG == 1
extern "C" void STPLOG_init(const char *);
#endif

static void parseArguments(int argc, char **argv)
{
	std::vector<std::string>	args;
	int				numArgs;
	const char			**argArray;

	for (int i=1; i<argc; i++) {
		if (strcmp(argv[i],"--read-args") == 0 && i+1 < argc) {
			CmdArgs::readArgumentsFromFile(argv[++i], args);
		} else {
			args.push_back(argv[i]);
		}
	}

	numArgs = args.size() + 1;
	argArray = new const char*[numArgs+1];
	argArray[0] = argv[0];
	argArray[numArgs] = 0;
	for (int i=1; i < numArgs; i++)
		argArray[i] = args[i-1].c_str();

	cl::ParseCommandLineOptions(
		numArgs,
		const_cast<char**>(argArray),
		" klee\n");

	delete[] argArray;
}

static Interpreter *theInterpreter = 0;
static bool interrupted = false;

// Pulled out so it can be easily called from a debugger.
extern "C" void halt_execution() { theInterpreter->setHaltExecution(true); }
extern "C" void stop_forking() { theInterpreter->setInhibitForking(true); }

static void interrupt_handle()
{
	if (interrupted) goto failed;
	if (theInterpreter == NULL) goto failed;

	std::cerr << "KLEE: ctrl-c detected, requesting interpreter to halt.\n";

	halt_execution();
	sys::SetInterruptFunction(interrupt_handle);

	interrupted = true;
  	return;

failed:
	std::cerr << "KLEE: ctrl-c detected, exiting.\n";
	exit(1);
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

	//fprintf(stderr, "KLEE: WATCHDOG: running: %s\n", buffer);
	if (system(buffer)==-1)
		perror("system");
}

// returns the end of the string put in buf
static char *format_tdiff(char *buf, long seconds)
{
	assert(seconds >= 0);

	long minutes = seconds / 60;  seconds %= 60;
	long hours   = minutes / 60;  minutes %= 60;
	long days    = hours   / 24;  hours   %= 24;

	if (days > 0) buf += sprintf(buf, "%ld days, ", days);
	buf += sprintf(buf, "%02ld:%02ld:%02ld", hours, minutes, seconds);
	return buf;
}

static char *format_tdiff(char *buf, double seconds) {
    long int_seconds = static_cast<long> (seconds);
    buf = format_tdiff(buf, int_seconds);
    buf += sprintf(buf, ".%02d", static_cast<int> (100 * (seconds - int_seconds)));
    return buf;
}

static void printTimes(PrefixWriter& info, struct tms* tms, clock_t* tm, time_t* t)
{
	char buf[256], *pbuf;
	bool tms_valid = true;

	t[1] = time(NULL);
	tm[1] = times(&tms[1]);
	if (tm[1] == (clock_t) -1) {
		perror("times");
		tms_valid = false;
	}

	strftime(
		buf, sizeof(buf),
		"Finished: %Y-%m-%d %H:%M:%S\n", localtime(&t[1]));
	info << buf;

	pbuf = buf;
	pbuf += sprintf(buf, "Elapsed: ");

#define FMT_TDIFF(x,y) format_tdiff(pbuf, (x - y) / (double)clk_tck)
	if (tms_valid) {
		const long clk_tck = sysconf(_SC_CLK_TCK);
		pbuf = FMT_TDIFF(tm[1], tm[0]);
		pbuf += sprintf(pbuf, " (user ");
		pbuf = FMT_TDIFF(tms[1].tms_utime, tms[0].tms_utime);
		*pbuf++ = '+';
		pbuf = FMT_TDIFF(tms[1].tms_cutime, tms[0].tms_cutime);
		pbuf += sprintf(pbuf, ", sys ");
		pbuf = FMT_TDIFF(tms[1].tms_stime, tms[0].tms_stime);
		*pbuf++ = '+';
		pbuf = FMT_TDIFF(tms[1].tms_cstime, tms[0].tms_cstime);
		pbuf += sprintf(pbuf, ")");
	} else
		pbuf = format_tdiff(pbuf, t[1] - t[0]);

	strcpy(pbuf, "\n");
	info << buf;
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

	info	<< "done: total queries = " << queries << " ("
		<< "valid: " << queriesValid << ", "
		<< "invalid: " << queriesInvalid << ", "
		<< "failed: " << queriesFailed << ", "
		<< "cex: " << queryCounterexamples << ")\n";

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

static int runWatchdog(void)
{
	if (MaxTime==0)
		klee_error("--watchdog used without --max-time");

	int pid = fork();

	if (pid<0)
		klee_error("unable to fork watchdog");

	fprintf(stderr, "KLEE: WATCHDOG: watching %d\n", pid);
	fflush(stderr);

	double nextStep = util::getWallTime() + MaxTime*1.1;
	int level = 0;

	// Simple stupid code...
	while (1) {
		int	status, res;
		double	time;

		sleep(1);

		res = waitpid(pid, &status, WNOHANG);
		if (res < 0) {
			if (errno==ECHILD) {
				// No child, no need to watch but
				// return error since we didn't catch
				// the exit.
				fprintf(stderr, "KLEE: watchdog (no child)\n");
				return 1;
			} else if (errno!=EINTR) {
				perror("watchdog waitpid");
				exit(1);
			}
			continue;
		}
		
		if (res==pid && WIFEXITED(status))
			return WEXITSTATUS(status);

		time = util::getWallTime();
		if (time < nextStep) continue;

		++level;

		fprintf(stderr, "KLEE: WATCHDOG: ");
		if (level==1) {
			fprintf(stderr, "time expired, attempting halt via INT\n");
			kill(pid, SIGINT);
		} else if (level==2) {
			fprintf(stderr, "time expired, attempting halt via gdb\n");
			halt_via_gdb(pid);
		} else {
			fprintf(stderr, "kill(9)ing child (I tried to be nice)\n");
			kill(pid, SIGKILL);
			return 1; // what more can we do
		}

		// Ideally this triggers a dump, which may take a while,
		// so try and give the process extra time to clean up.
		nextStep = util::getWallTime() + std::max(15., MaxTime*.1);
	}

	return 0;
}

static void runReplay(Interpreter* interpreter, Function* mainFn, CmdArgs* ca)
{
  std::vector<std::string> outFiles = ReplayKTestFile;
  foreach (it, ReplayKTestDir.begin(),  ReplayKTestDir.end())
    KleeHandler::getOutFiles(*it, outFiles);

  std::vector<KTest*> kTests;
  foreach (it, outFiles.begin(), outFiles.end()) {
    KTest *out = kTest_fromFile(it->c_str());
    if (out) {
      kTests.push_back(out);
    } else {
      std::cerr << "KLEE: unable to open: " << *it << "\n";
    }
  }

  if (RunInDir != "") {
    int res = chdir(RunInDir.c_str());
    if (res < 0) {
      klee_error("Unable to change directory to: %s", RunInDir.c_str());
    }
  }

  unsigned i=0;
  foreach (it, kTests.begin(), kTests.end()) {
    KTest *out = *it;
    interpreter->setReplayKTest(out);
    std::cerr	<< "KLEE: replaying: " << *it
    		<< " (" << kTest_numBytes(out) << " bytes)"
		<< " (" << ++i << "/" << outFiles.size() << ")\n";

    // XXX should put envp in .ktest ?
    interpreter->runFunctionAsMain(
    	mainFn, out->numArgs, out->args, ca->getEnvp());
    if (interrupted) break;
  }

  interpreter->setReplayKTest(0);

  while (!kTests.empty()) {
    kTest_free(kTests.back());
    kTests.pop_back();
  }
}

static void runSeeds(
	SeedExecutor<ExecutorBC> *exe,
	Function* mainFn,
	CmdArgs* ca)
{
	std::vector<KTest *>	seeds;

	foreach (it, SeedOutFile.begin(), SeedOutFile.end()) {
		KTest *out = kTest_fromFile(it->c_str());
		if (!out) {
			std::cerr << "KLEE: unable to open: " << *it << "\n";
			exit(1);
		}
		seeds.push_back(out);
	}

	foreach (it, SeedOutDir.begin(), SeedOutDir.end()) {
		std::vector<std::string> outFiles;
		KleeHandler::getOutFiles(*it, outFiles);
		foreach (it2, outFiles.begin(), outFiles.end()) {
			KTest *out = kTest_fromFile(it2->c_str());
			if (!out) {
				std::cerr << "KLEE: unable to open: "
					<< *it2 << "\n";
				exit(1);
			}
			seeds.push_back(out);
		}
		if (outFiles.empty()) {
			std::cerr	<< "KLEE: seeds directory is empty: "
					<< *it << "\n";
			exit(1);
		}
	}

	if (!seeds.empty()) {
		std::cerr << "KLEE: using " << seeds.size() << " seeds\n";
		exe->useSeeds(&seeds);
	}

	if (RunInDir != "") {
		int res = chdir(RunInDir.c_str());
		if (res < 0) {
			klee_error(
				"Unable to change directory to: %s",
				RunInDir.c_str());
		}
	}
	exe->runFunctionAsMain(
		mainFn, ca->getArgc(), ca->getArgv(), ca->getEnvp());

	while (!seeds.empty()) {
		kTest_free(seeds.back());
		seeds.pop_back();
	}
}

int main(int argc, char **argv, char **envp)
{
#if ENABLE_STPLOG == 1
	STPLOG_init("stplog.c");
#endif
	bool				useSeeds;
	Module				*mainModule;
	const Module			*finalModule;
	KleeHandler			*handler;
	CmdArgs				*ca;
	Function			*mainFn;
	Interpreter			*interpreter;
	ExecutorBC			*exe_bc;
	SeedExecutor<ExecutorBC>	*exe_seed;
	std::vector<std::string>	pathFiles;
	std::list<ReplayPath>		replayPaths;
	std::vector<std::string>	arguments;

	atexit(llvm_shutdown);  // Call llvm_shutdown() on exit.

	llvm::InitializeNativeTarget();

	parseArguments(argc, argv);
	sys::PrintStackTraceOnErrorSignal();

	g_InputFile = InputFile;
	g_Libc = Libc;

	g_WithPOSIXRuntime = WithPOSIXRuntime;
	useSeeds = ReplayKTestDir.empty() && ReplayKTestFile.empty();

	if (Watchdog) return runWatchdog();

	sys::SetInterruptFunction(interrupt_handle);

	// Load the bytecode...
	Interpreter::ModuleOptions Opts = getMainModule(mainModule);

	// Get the desired main function.  klee_main initializes uClibc
	// locale and other data and then calls main.
	mainFn = mainModule->getFunction("main");
	if (mainFn == NULL) {
		std::cerr << "'main' function not found in module.\n";
		return -1;
	}

	if (ReplayPathDir != "")
		KleeHandler::getPathFiles(ReplayPathDir, pathFiles);
	if (ReplayPathFile != "")
		pathFiles.push_back(ReplayPathFile);

	KleeHandler::loadPathFiles(pathFiles, replayPaths);

	arguments.push_back(InputFile);
	arguments.insert(arguments.end(), InputArgv.begin(), InputArgv.end());
	ca = new CmdArgs(InputFile, Environ, envp, arguments);

	handler = new KleeHandler(ca);
	if (useSeeds) {
		exe_seed = new SeedExecutor<ExecutorBC>(handler);
		interpreter = exe_seed;
	} else {
		exe_bc = new ExecutorBC(handler);
		interpreter = exe_bc;
	}
	theInterpreter = interpreter;
	handler->setInterpreter(interpreter);

	std::ostream &infoFile = handler->getInfoStream();
	for (int i=0; i<argc; i++) {
		infoFile << argv[i] << (i+1<argc ? " ":"\n");
	}

	TwoOStreams info2s(&std::cerr, &infoFile);
	PrefixWriter info(info2s, "KLEE: ");
	info << "PID: " << getpid() << "\n";

	finalModule = interpreter->setModule(mainModule, Opts);
	externalsAndGlobalsCheck(finalModule);

	if (!replayPaths.empty()) {
		interpreter->setReplayPaths(&replayPaths);
	}

	char buf[256];
	time_t t[2];
	clock_t tm[2];
	struct tms tms[2];
	bool tms_valid = true;

	t[0] = time(NULL);
	tm[0] = times(&tms[0]);
	if (tm[0] == (clock_t) -1) {
		perror("times");
		tms_valid = false;
	}
	strftime(
		buf, sizeof(buf),
		"Started: %Y-%m-%d %H:%M:%S\n", localtime(&t[0]));
	info << buf;
	infoFile.flush();

	if (!useSeeds) {
		assert(SeedOutFile.empty());
		assert(SeedOutDir.empty());
		runReplay(interpreter, mainFn, ca);
	} else {
		runSeeds(exe_seed, mainFn, ca);
	}

	printTimes(info, tms, tm, t);

	delete interpreter;

	printStats(info, handler);

	delete handler;
	delete ca;

	return 0;
}

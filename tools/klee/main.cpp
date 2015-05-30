/* -*- mode: c++; c-basic-offset: 2; -*- */

#include "../../lib/Skins/SeedExecutor.h"
#include "../../lib/Skins/KTestExecutor.h"
#include "../../lib/Core/ExecutorBC.h"
#include "klee/Common.h"
#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Interpreter.h"
#include "klee/Statistics.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/KleeHandler.h"
#include "klee/Internal/ADT/CmdArgs.h"
#include "klee/Internal/System/Time.h"
#include "static/Sugar.h"

#include "libc.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/ManagedStatic.h>

// FIXME: Ugh, this is gross. But otherwise our config.h conflicts with LLVMs.
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include <llvm/Support/TargetSelect.h>
#include "llvm/Support/Signals.h"
#include <iostream>
#include <cerrno>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <iterator>

using namespace llvm;
using namespace klee;

extern void externalsAndGlobalsCheck(const Module *m);
extern ModuleOptions getMainModule(std::unique_ptr<Module> &m);

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

  cl::list<std::string> SeedKTestFile("seed-out");

  cl::list<std::string> SeedKTestDir("seed-out-dir");

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

static void runReplayKTest(
	Interpreter* interpreter, Function* mainFn, CmdArgs* ca)
{
	std::vector<KTest*>		kTests;
	std::vector<std::string>	outDirs(
		ReplayKTestDir.begin(),
		ReplayKTestDir.end()),
					outFiles(
		ReplayKTestFile.begin(),
		ReplayKTestFile.end());

	KleeHandler::getKTests(outFiles, outFiles, kTests);

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
	std::vector<KTest*>		seeds;
	std::vector<std::string>	outDirs(
		SeedKTestDir.begin(),
		SeedKTestDir.end()),
					outFiles(
		SeedKTestFile.begin(),
		SeedKTestFile.end());

	KleeHandler::getKTests(outFiles, outDirs, seeds);

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
	std::unique_ptr<Module>		mainModule;
	const Module			*finalModule;
	KleeHandler			*handler;
	CmdArgs				*ca;
	Function			*mainFn;
	Interpreter			*interpreter;
	SeedExecutor<ExecutorBC>	*exe_seed;
	KTestExecutor<ExecutorBC>	*exe_ktest;
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
	ModuleOptions Opts = getMainModule(mainModule);

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
		exe_ktest = new KTestExecutor<ExecutorBC>(handler);
		interpreter = exe_ktest;
	}
	theInterpreter = interpreter;
	handler->setInterpreter(interpreter);
	handler->printInfoHeader(argc, argv);

	finalModule = interpreter->setModule(mainModule.get(), Opts);
	externalsAndGlobalsCheck(finalModule);

	if (!replayPaths.empty()) {
		interpreter->setReplay(new ReplayBrPaths(replayPaths));
	}

	if (!useSeeds) {
		assert(SeedKTestFile.empty());
		assert(SeedKTestDir.empty());
		runReplayKTest(interpreter, mainFn, ca);
	} else {
		runSeeds(exe_seed, mainFn, ca);
	}

	handler->printInfoFooter();

	delete interpreter;
	delete handler;
	delete ca;

	return 0;
}

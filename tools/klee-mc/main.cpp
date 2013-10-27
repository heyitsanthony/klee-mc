/* -*- mode: c++; c-basic-offset: 2; -*- */

#include "klee/Common.h"
#include "klee/Interpreter.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/Statistics.h"
#include "klee/Internal/System/Time.h"
#include "klee/Internal/Support/Watchdog.h"
#include "static/Sugar.h"
#include "../../lib/Skins/GDBExecutor.h"
#include "../../lib/Skins/ShadowExecutor.h"
#include "../../lib/Skins/TaintMergeExecutor.h"
#include "../../lib/Skins/ConstraintSeedExecutor.h"
#include "../../lib/Skins/DDTExecutor.h"
#include "../../lib/Skins/KTestExecutor.h"
#include "KleeHandlerVex.h"
#include "klee/Internal/ADT/CmdArgs.h"

#include "guest.h"
#include "guestptimg.h"
#include "guestfragment.h"
#include "elfimg.h"
#include "guestelf.h"
#include "ExecutorVex.h"
#include "ExeSymHook.h"
#include "ExeChk.h"

// FIXME: Ugh, this is gross. But otherwise our config.h conflicts with LLVMs.
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/Signals.h>

#include <iostream>
#include <fstream>
#include <sstream>

using namespace llvm;
using namespace klee;

bool			SymArgs, SymArgC;
extern double		MaxSTPTime;
extern bool		WriteTraces;
extern double		MaxTime;

namespace {
	cl::opt<bool, true> SymArgsProxy(
		"symargs",
		cl::desc("Make argument strings symbolic"),
		cl::location(SymArgs),
		cl::init(false));

	cl::opt<bool, true> SymArgCProxy(
		"symargc",
		cl::desc("Make argc symbolic"),
		cl::location(SymArgC),
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
	XChkJIT("xchkjit",
		cl::desc("Cross check concrete / no syscall binary with JIT."));

	cl::opt<bool>
	UseWatchdog("watchdog",
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

static std::vector<KTest*>	kTests;

static bool isReplayingKTest(void)
{ return (!ReplayKTestDir.empty() || !ReplayKTestFile.empty()); }

static void loadKTests(void)
{
	if (!isReplayingKTest()) return;

	std::vector<std::string>	outDirs(
		ReplayKTestDir.begin(),
		ReplayKTestDir.end()),
					outFiles(
		ReplayKTestFile.begin(),
		ReplayKTestFile.end());

	KleeHandler::getKTests(outFiles, outDirs, kTests);
}

void runReplayKTest(Interpreter* interpreter)
{
	ExecutorVex			*ev;
	unsigned			i=0;

	ev = dynamic_cast<ExecutorVex*>(interpreter);
	foreach (it, kTests.begin(), kTests.end()) {
		KTest *out = *it;

		interpreter->setReplayKTest(out);
		std::cerr
			<< "KLEE: replaying: " << *it << " ("
			<< kTest_numBytes(out) << " bytes)"
			<< " (" << ++i << "/" << kTests.size() << ")\n";

		// XXX should put envp in .ktest ?
		ev->runImage();
		if (ev->isHalted())
			break;
	}

	interpreter->setReplayKTest(0);
}

static CmdArgs* getCmdArgs(char** envp)
{
	std::vector<std::string>	input_args;
	CmdArgs				*ret;
	const std::string		env_path(Environ);

	foreach (it, InputArgv.begin(), InputArgv.end())
		input_args.push_back(*it);

	ret = new CmdArgs(InputFile, env_path, envp, input_args);
	if (SymArgs) ret->setSymbolic();
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

#define NEW_INTERP(x)							\
	((UseGDB) ? new GDBExecutor<x>(handler) 			\
	: (UseConstraintSeed) ? new ConstraintSeedExecutor<x>(handler)	\
	: (UseDDT) ? new DDTExecutor<x>(handler)			\
	: (UseTaintMerge) ? new TaintMergeExecutor<x>(handler)		\
	: (UseTaint) ? new ShadowExecutor<x>(handler)			\
	: new x(handler))

#define NEW_INTERP_KTEST(x)						\
	((UseGDB) ? new GDBExecutor<KTestExecutor<x> >(handler) 	\
	: (UseConstraintSeed) ?						\
		new ConstraintSeedExecutor<KTestExecutor<x> >(handler)	\
	: new KTestExecutor<x>(handler))

Interpreter* createInterpreter(KleeHandler *handler, Guest* gs)
{
	if (isReplayingKTest() && Replay::isSuppressForks()) {
		/* suppressed forks */
		assert (!UseDDT && !UseTaintMerge && !UseTaint);

		if (SymHook) return NEW_INTERP_KTEST(ExeSymHook);
		if (XChkJIT) return NEW_INTERP_KTEST(ExeChk);

		return NEW_INTERP_KTEST(ExecutorVex);
	}

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

	interpreter->setReplay(new ReplayBrPaths(replayPaths));
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

	/* important: can not declare watchdog near top because
	 * arguments aren't initialized I guess */
	Watchdog	wd((UseWatchdog) ? MaxTime : 0);

	cmdargs = getCmdArgs(envp);
	gs = getGuest(cmdargs);
	if (gs == NULL) {
		fprintf(stderr, "[klee-mc] Could not get guest.\n");
		return 2;
	}

	handler = new KleeHandlerVex(cmdargs, gs);

	interpreter = createInterpreter(handler, gs);

	theInterpreter = interpreter;
	handler->setInterpreter(interpreter);
	handler->printInfoHeader(argc, argv);

	loadKTests();
	setupReplayPaths(interpreter);

	if (isReplayingKTest() && Replay::isSuppressForks()) {
		/* directly feed concrete ktests data into states. yuck */
		runReplayKTest(interpreter);
	} else {
		/* ktests with forking */
		if (isReplayingKTest() && !kTests.empty()) {
			assert (replayPaths.empty() && "grr replay paths");
			interpreter->setReplay(new ReplayKTests(kTests));
		}
		dynamic_cast<ExecutorVex*>(interpreter)->runImage();
	}

	while (!kTests.empty()) {
		kTest_free(kTests.back());
		kTests.pop_back();
	}

	handler->printInfoFooter();

	delete interpreter;
	delete gs;
	delete handler;
	delete cmdargs;

	llvm_shutdown();

	return 0;
}

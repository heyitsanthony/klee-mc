#include "../../lib/Skins/SeedExecutor.h"
#include "../../lib/Skins/KTestExecutor.h"
#include "klee/Common.h"
#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Interpreter.h"
#include "klee/Statistics.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/KleeHandler.h"
#include "klee/Internal/ADT/CmdArgs.h"
#include "klee/Internal/System/Time.h"
#include "klee/Internal/Support/Watchdog.h"
#include "static/Sugar.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/Support/system_error.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/ManagedStatic.h>

// FIXME: Ugh, this is gross. But otherwise our config.h conflicts with LLVMs.
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/Signals.h>
#include <iostream>
#include <cerrno>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/times.h>
#include <signal.h>
#include <iterator>

#include "ExecutorJ.h"
#include "JnJavaName.h"

using namespace llvm;
using namespace klee;

#define GCTX	getGlobalContext()

extern double	MaxTime;
extern bool	WriteTraces;



namespace {
	cl::opt<std::string>
	InputFile(
		cl::desc("<input bytecode>"),
		cl::Positional,
		cl::init("-"));

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

	cl::opt<bool> UseWatchdog(
		"watchdog",
		cl::desc("Use a watchdog process to enforce --max-time."));
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

static void runReplayKTest(Interpreter* interpreter)
{
	unsigned i = 0;

	loadKTests();

	foreach (it, kTests.begin(), kTests.end()) {
		KTest *out = *it;

		interpreter->setReplayKTest(out);
		std::cerr
			<< "KLEE: replaying: " << *it << " ("
			<< kTest_numBytes(out) << " bytes)"
			<< " (" << ++i << "/" << kTests.size() << ")\n";
		assert (0 == 1 && "STUB");
		#if 0
		// XXX should put envp in .ktest ?
		ev->runImage();
		if (ev->isHalted())
			break;
		#endif
	}

	interpreter->setReplayKTest(0);
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

void loadInputBitcode(const std::string& ifile, Module* &mainModule)
{
	std::string ErrorMsg;
	OwningPtr<MemoryBuffer> Buffer;

	std::cerr << "INPUT FILE: " << ifile << '\n';
	MemoryBuffer::getFileOrSTDIN(ifile.c_str(), Buffer);


	mainModule = NULL;
	if (!Buffer) goto done;
	
	mainModule = ParseBitcodeFile(Buffer.get(), GCTX, &ErrorMsg);
	if (mainModule == NULL) {
		Buffer.reset();
		goto done;
	}

	if (mainModule->MaterializeAllPermanently(&ErrorMsg)) {
		delete mainModule;
		mainModule = NULL;
		goto done;
	}

done:
	if (mainModule == NULL)
		klee_error(
			"error loading program '%s': %s",
			ifile.c_str(),
			ErrorMsg.c_str());

	assert(mainModule && "unable to materialize");
}


ModuleOptions getClassModule(Module* &mainModule)
{
	std::vector<std::string>	exclude;

	loadInputBitcode(InputFile, mainModule);

	ModuleOptions Opts(false, false, exclude);

//	mainModule = setupLibc(mainModule, Opts);
//	loadIntrinsics(Opts);
//	loadPOSIX(mainModule, Opts);

	return Opts;
}


int main(int argc, char **argv, char **envp)
{
	Module				*mainModule;
	const Module			*finalModule;
	KleeHandler			*handler;
	Interpreter			*interpreter;
//	SeedExecutor<ExecutorBC>	*exe_seed;
//	KTestExecutor<ExecutorBC>	*exe_ktest;
	std::vector<std::string>	pathFiles;
	std::list<ReplayPath>		replayPaths;
	bool				useSeeds;

	atexit(llvm_shutdown);
	llvm::InitializeNativeTarget();
	llvm::cl::ParseCommandLineOptions(argc, argv);

	sys::SetInterruptFunction(interrupt_handle);

	useSeeds = ReplayKTestDir.empty() && ReplayKTestFile.empty();

	/* important: can not declare watchdog near top because
	 * arguments aren't initialized I guess */
	Watchdog	wd((UseWatchdog) ? MaxTime : 0);

	// Load the bytecode...
	ModuleOptions Opts = getClassModule(mainModule);

	handler = new KleeHandler();
	interpreter = new ExecutorJ(handler);
	theInterpreter = interpreter;

	setupReplayPaths(interpreter);

	KleeHandler::loadPathFiles(pathFiles, replayPaths);

	handler->setInterpreter(interpreter);
	handler->printInfoHeader(argc, argv);

	finalModule = interpreter->setModule(mainModule, Opts);
//	externalsAndGlobalsCheck(finalModule);

	std::cerr << "DO THE RUN\n";
	if (isReplayingKTest() && Replay::isSuppressForks()) {
		/* directly feed concrete ktests data into states. yuck */
		runReplayKTest(interpreter);
	} else {
		/* ktests with forking */
		if (isReplayingKTest() && !kTests.empty()) {
			assert (replayPaths.empty() && "grr replay paths");
			interpreter->setReplay(new ReplayKTests(kTests));
		}
		//dynamic_cast<ExecutorVex*>(interpreter)->runImage();
		assert (0 == 1 && "UGH");
	}

	while (!kTests.empty()) {
		kTest_free(kTests.back());
		kTests.pop_back();
	}
	
	handler->printInfoFooter();

	delete interpreter;
	delete handler;

	return 0;
}

/* -*- mode: c++; c-basic-offset: 2; -*- */

#include "klee/Common.h"
#include "klee/Interpreter.h"
#include "klee/Statistics.h"
#include "klee/Internal/System/Time.h"
#include "static/Sugar.h"

#include "KleeHandler.h"
#include "cmdargs.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"

#include "gueststate.h"
#include "gueststateptimg.h"
#include "ExecutorVex.h"

//#include "llvm/Support/system_error.h"

// FIXME: Ugh, this is gross. But otherwise our config.h conflicts with LLVMs.
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include "llvm/Target/TargetSelect.h"
//#include "llvm/Support/Signals.h"
#include "llvm/System/Signals.h"
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

extern Interpreter::ModuleOptions getMainModule(Module* &m);

namespace {
  cl::opt<std::string>
  InputFile(cl::desc("<input executable>"), cl::Positional, cl::init("-"));

  cl::opt<std::string>
  RunInDir("run-in", cl::desc("Change to the given directory prior to executing"));

  cl::opt<std::string>
  Environ("environ", cl::desc("Parse environ from given file (in \"env\" format)"));

  cl::list<std::string>
  InputArgv(cl::ConsumeAfter, 
            cl::desc("<program arguments>..."));

  // this is a fake entry, its automagically handled
  cl::list<std::string>
  ReadArgsFilesFake("read-args", 
                    cl::desc("File to read arguments from (one arg per line)"));
    
  cl::opt<bool>
  ReplayKeepSymbolic("replay-keep-symbolic", 
                     cl::desc("Replay the test cases only by asserting"
                              "the bytes, not necessarily making them concrete."));
    
  cl::list<std::string>
  ReplayOutFile("replay-out",
                cl::desc("Specify an out file to replay"),
                cl::value_desc("out file"));

  cl::list<std::string>
  ReplayOutDir("replay-out-dir",
	       cl::desc("Specify a directory to replay .out files from"),
	       cl::value_desc("out directory"));

  cl::opt<std::string>
  ReplayPathFile("replay-path",
                 cl::desc("Specify a path file to replay"),
                 cl::value_desc("path file"));

  cl::opt<std::string>
  ReplayPathDir("replay-path-dir",
          cl::desc("Specify a directory to replay path files from"),
          cl::value_desc("path directory"));

  cl::list<std::string>
  SeedOutFile("seed-out");
  
  cl::list<std::string>
  SeedOutDir("seed-out-dir");
  
  cl::opt<unsigned>
  MakeConcreteSymbolic("make-concrete-symbolic",
                       cl::desc("Rate at which to make concrete reads symbolic (0=off)"),
                       cl::init(0));

  cl::opt<bool>
  Watchdog("watchdog",
           cl::desc("Use a watchdog process to enforce --max-time."),
           cl::init(0));
}

// This is a terrible hack until we get some real modelling of the
// system. All we do is check the undefined symbols and m and warn about
// any "unrecognized" externals and about any obviously unsafe ones.
static Interpreter *theInterpreter = 0;
static bool interrupted = false;


extern bool WriteTraces;
extern cl::opt<double> MaxTime;

/***/
//===----------------------------------------------------------------------===//
// main Driver function
//
#if ENABLE_STPLOG == 1
extern "C" void STPLOG_init(const char *);
#endif

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

  cl::ParseCommandLineOptions(numArgs, (char**) argArray, " klee\n");
  delete[] argArray;
}

// Pulled out so it can be easily called from a debugger.
extern "C"
void halt_execution() {
  theInterpreter->setHaltExecution(true);
}

extern "C"
void stop_forking() {
  theInterpreter->setInhibitForking(true);
}

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

struct TwoOStreams
{
    std::ostream* s[2];

    TwoOStreams(std::ostream* s0, std::ostream* s1) { s[0] = s0; s[1] = s1; }

    template <typename T>
    TwoOStreams& operator<<(const T& t) {
        *s[0] << t;
        *s[1] << t;
        return *this;
    }

    TwoOStreams& operator<<(std::ostream& (*f)(std::ostream&)) {
        *s[0] << f;
        *s[1] << f;
        return *this;
    }
};

class PrefixWriter
{
    TwoOStreams* streams;
    const char* prefix;

public:
    PrefixWriter(TwoOStreams& s, const char* p) : streams(&s), prefix(p) { }

    operator TwoOStreams&() const {
        *streams->s[0] << prefix;
        return *streams;
    }

    template <typename T>
    TwoOStreams& operator<<(const T& t) {
        static_cast<TwoOStreams&>(*this) << t;
        return *streams;
    }

    TwoOStreams& operator<<(std::ostream& (*f)(std::ostream&)) {
        static_cast<TwoOStreams&>(*this) << f;
        return *streams;
    }
};

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
			klee_error("Unable to change directory to: %s", RunInDir.c_str());
		}
	}

	exe->runImage();	
}

static void printStats(PrefixWriter& info, KleeHandler* handler)
{
  uint64_t queries = 
    *theStatisticManager->getStatisticByName("Queries");
  uint64_t queriesValid = 
    *theStatisticManager->getStatisticByName("QueriesValid");
  uint64_t queriesInvalid = 
    *theStatisticManager->getStatisticByName("QueriesInvalid");
  uint64_t queryCounterexamples = 
    *theStatisticManager->getStatisticByName("QueriesCEX");
  uint64_t queryConstructs = 
    *theStatisticManager->getStatisticByName("QueriesConstructs");
  uint64_t queryCacheHits =
    *theStatisticManager->getStatisticByName("QueryCacheHits");
  uint64_t queryCacheMisses =
    *theStatisticManager->getStatisticByName("QueryCacheMisses");
  uint64_t instructions = 
    *theStatisticManager->getStatisticByName("Instructions");
  uint64_t forks = 
    *theStatisticManager->getStatisticByName("Forks");

  info << "done: total queries = " << queries << " ("
       << "valid: " << queriesValid << ", "
       << "invalid: " << queriesInvalid << ", "
       << "cex: " << queryCounterexamples << ")\n";
  if (queries)
    info << "done: avg. constructs per query = " 
         << queryConstructs / queries << "\n";  

  info << "done: query cache hits = " << queryCacheHits << ", "
       << "query cache misses = " << queryCacheMisses << "\n";

  info << "done: total instructions = " << instructions << "\n";
  info << "done: explored paths = " << 1 + forks << "\n";
  info << "done: completed paths = " << handler->getNumPathsExplored() << "\n";
  info << "done: generated tests = " << handler->getNumTestCases() << "\n";
}

static int runWatchdog(void)
{
  if (MaxTime==0)  klee_error("--watchdog used without --max-time");

  int pid = fork();
  if (pid<0) klee_error("unable to fork watchdog");
  fprintf(stderr, "KLEE: WATCHDOG: watching %d\n", pid);
  fflush(stderr);

  double nextStep = util::getWallTime() + MaxTime*1.1;
  int level = 0;

  // Simple stupid code...
  while (1) {
    sleep(1);
    int status, res = waitpid(pid, &status, WNOHANG);

    if (res < 0) {
      if (errno==ECHILD) { // No child, no need to watch but
                       // return error since we didn't catch
                       // the exit.
        fprintf(stderr, "KLEE: watchdog exiting (no child)\n");
        return 1;
      } else if (errno!=EINTR) {
        perror("watchdog waitpid");
        exit(1);
      }
    } else if (res==pid && WIFEXITED(status)) {
      return WEXITSTATUS(status);
    } else {
      double time = util::getWallTime();

      if (time > nextStep) {
        ++level;
    
        if (level==1) {
          fprintf(stderr, "KLEE: WATCHDOG: time expired, attempting halt via INT\n");
          kill(pid, SIGINT);
        } else if (level==2) {
          fprintf(stderr, "KLEE: WATCHDOG: time expired, attempting halt via gdb\n");
          halt_via_gdb(pid);
        } else {
          fprintf(stderr, "KLEE: WATCHDOG: kill(9)ing child (I tried to be nice)\n");
          kill(pid, SIGKILL);
          return 1; // what more can we do
        }

        // Ideally this triggers a dump, which may take a while,
        // so try and give the process extra time to clean up.
        nextStep = util::getWallTime() + std::max(15., MaxTime*.1);
      }
    }
  }

  return 0;
}

static CmdArgs* getCmdArgs(char** envp)
{
	std::list<std::string>	input_args;
	const std::string	env_path(Environ);

	foreach (it, InputArgv.begin(), InputArgv.end())
		input_args.push_back(*it);

	return new CmdArgs(InputFile, env_path, envp, input_args);
}

int main(int argc, char **argv, char **envp)
{
	Interpreter::InterpreterOptions IOpts;
	KleeHandler	*handler;
	CmdArgs		*cmdargs;
	GuestState	*gs;
	Interpreter	*interpreter;

	//  std::list<Interpreter::ReplayPathType> replayPaths;

	#if ENABLE_STPLOG == 1
	STPLOG_init("stplog.c");
	#endif

	atexit(llvm_shutdown);  // Call llvm_shutdown() on exit.

	llvm::InitializeNativeTarget();

	parseArguments(argc, argv);
	sys::PrintStackTraceOnErrorSignal();

	if (Watchdog) return runWatchdog();

	sys::SetInterruptFunction(interrupt_handle);

	//  replayPaths = getReplayPaths();
	cmdargs = getCmdArgs(envp);

	IOpts.MakeConcreteSymbolic = MakeConcreteSymbolic;
	handler = new KleeHandler(cmdargs);


	gs = GuestStatePTImg::create<GuestStatePTImg>(
		cmdargs->getArgc(),
		cmdargs->getArgv(),
		cmdargs->getEnvp());
	
	interpreter = new ExecutorVex(IOpts, handler, gs);
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


	#if 0
	if (!replayPaths.empty()) {
	interpreter->setReplayPaths(&replayPaths);
	}
	#endif

	run(dynamic_cast<ExecutorVex*>(interpreter));

	delete interpreter;

	printStats(info, handler);
	delete handler;

	delete cmdargs;

	return 0;
}

/* -*- mode: c++; c-basic-offset: 2; -*- */

#include "klee/Common.h"
#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Interpreter.h"
#include "klee/Statistics.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/Internal/ADT/TreeStream.h"
#include "klee/Internal/System/Time.h"
#include "static/Sugar.h"

#include "KleeHandler.h"
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
  InputArgv(cl::ConsumeAfter, 
            cl::desc("<program arguments>..."));

 
 
   
   
    
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
  WithPOSIXRuntime("posix-runtime", 
		cl::desc("Link with POSIX runtime"),
		cl::init(false));
    
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

extern bool WriteTraces;
extern cl::opt<double> MaxTime;
std::string g_InputFile = InputFile;
LibcType g_Libc = Libc;
bool g_WithPOSIXRuntime;

/***/
//===----------------------------------------------------------------------===//
// main Driver function
//
#if ENABLE_STPLOG == 1
extern "C" void STPLOG_init(const char *);
#endif

static std::string strip(std::string &in) {
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
    line = strip(line);
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

// This is a terrible hack until we get some real modelling of the
// system. All we do is check the undefined symbols and m and warn about
// any "unrecognized" externals and about any obviously unsafe ones.
static Interpreter *theInterpreter = 0;

static bool interrupted = false;

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
static void halt_via_gdb(int pid) {
  char buffer[256];
  sprintf(buffer, 
          "gdb --batch --eval-command=\"p halt_execution()\" "
          "--eval-command=detach --pid=%d &> /dev/null",
          pid);
  //  fprintf(stderr, "KLEE: WATCHDOG: running: %s\n", buffer);
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
  strftime(buf, sizeof(buf), "Finished: %Y-%m-%d %H:%M:%S\n", localtime(&t[1]));
  info << buf;

  pbuf = buf;
  pbuf += sprintf(buf, "Elapsed: ");
  if (tms_valid) {
      const long clk_tck = sysconf(_SC_CLK_TCK);
      pbuf = format_tdiff(pbuf, (tm[1] - tm[0]) / (double) clk_tck);
      pbuf += sprintf(pbuf, " (user ");
      pbuf = format_tdiff(pbuf, (tms[1].tms_utime - tms[0].tms_utime) / (double) clk_tck);
      *pbuf++ = '+';
      pbuf = format_tdiff(pbuf, (tms[1].tms_cutime - tms[0].tms_cutime) / (double) clk_tck);
      pbuf += sprintf(pbuf, ", sys ");
      pbuf = format_tdiff(pbuf, (tms[1].tms_stime - tms[0].tms_stime) / (double) clk_tck);
      *pbuf++ = '+';
      pbuf = format_tdiff(pbuf, (tms[1].tms_cstime - tms[0].tms_cstime) / (double) clk_tck);
      pbuf += sprintf(pbuf, ")");
  } else
      pbuf = format_tdiff(pbuf, t[1] - t[0]);
  strcpy(pbuf, "\n");
  info << buf;
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

static char** getEnvironment(void)
{
  char **pEnvp;

  if (Environ == "") return NULL;

  std::vector<std::string> items;
  std::ifstream f(Environ.c_str());

  if (!f.good())
    klee_error("unable to open --environ file: %s", Environ.c_str());

  while (!f.eof()) {
    std::string line;
    std::getline(f, line);
    line = strip(line);
    if (!line.empty())
      items.push_back(line);
  }
  f.close();

  pEnvp = new char *[items.size()+1];
  unsigned i=0;
  for (; i != items.size(); ++i) pEnvp[i] = strdup(items[i].c_str());
  pEnvp[i] = NULL;

  return pEnvp;
}

static void runReplay(Interpreter* interpreter, Function* mainFn, char** pEnvp)
{
  std::vector<std::string> outFiles = ReplayOutFile;
  foreach (it, ReplayOutDir.begin(),  ReplayOutDir.end())
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
    interpreter->setReplayOut(out);
    std::cerr << "KLEE: replaying: " << *it << " (" << kTest_numBytes(out) << " bytes)"
               << " (" << ++i << "/" << outFiles.size() << ")\n";
    // XXX should put envp in .ktest ?
    interpreter->runFunctionAsMain(mainFn, out->numArgs, out->args, pEnvp);
    if (interrupted) break;
  }

  interpreter->setReplayOut(0);

  while (!kTests.empty()) {
    kTest_free(kTests.back());
    kTests.pop_back();
  }
}

static void runSeeds(
  Interpreter* interpreter, Function* mainFn,
  int pArgc, char** pArgv, char** pEnvp)
{
  std::vector<KTest *> seeds;
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
        std::cerr << "KLEE: unable to open: " << *it2 << "\n";
        exit(1);
      }
      seeds.push_back(out);
    }
    if (outFiles.empty()) {
      std::cerr << "KLEE: seeds directory is empty: " << *it << "\n";
      exit(1);
    }
  }
     
  if (!seeds.empty()) {
    std::cerr << "KLEE: using " << seeds.size() << " seeds\n";
    interpreter->useSeeds(&seeds);
  }
  if (RunInDir != "") {
    int res = chdir(RunInDir.c_str());
    if (res < 0) {
      klee_error("Unable to change directory to: %s", RunInDir.c_str());
    }
  }
  interpreter->runFunctionAsMain(mainFn, pArgc, pArgv, pEnvp);

  while (!seeds.empty()) {
    kTest_free(seeds.back());
    seeds.pop_back();
  }
}

int main(int argc, char **argv, char **envp) {  
#if ENABLE_STPLOG == 1
  STPLOG_init("stplog.c");
#endif

  atexit(llvm_shutdown);  // Call llvm_shutdown() on exit.

  llvm::InitializeNativeTarget();

  parseArguments(argc, argv);
  sys::PrintStackTraceOnErrorSignal();

  g_InputFile = InputFile;
  g_Libc = Libc;
  g_WithPOSIXRuntime = WithPOSIXRuntime;



  if (Watchdog) return runWatchdog();

  sys::SetInterruptFunction(interrupt_handle);

  // Load the bytecode...
  Module* mainModule;
  Interpreter::ModuleOptions Opts = getMainModule(mainModule);

  // Get the desired main function.  klee_main initializes uClibc
  // locale and other data and then calls main.
  Function *mainFn = mainModule->getFunction("main");
  if (!mainFn) {
    std::cerr << "'main' function not found in module.\n";
    return -1;
  }

  // FIXME: Change me to std types.
  char **pEnvp = getEnvironment();
  if (pEnvp == NULL) pEnvp = envp;

  int pArgc = InputArgv.size() + 1; 
  char** pArgv = new char *[pArgc];
  for (unsigned i=0; i<InputArgv.size()+1; i++) {
    std::string &arg = (i==0 ? InputFile : InputArgv[i-1]);
    unsigned size = arg.size() + 1;
    char *pArg = new char[size];
    
    std::copy(arg.begin(), arg.end(), pArg);
    pArg[size - 1] = 0;
    
    pArgv[i] = pArg;
  }

  std::vector<std::string> pathFiles;
  if (ReplayPathDir != "") KleeHandler::getPathFiles(ReplayPathDir, pathFiles);
  if (ReplayPathFile != "") pathFiles.push_back(ReplayPathFile);

  std::list<Interpreter::ReplayPathType> replayPaths;
  Interpreter::ReplayPathType replayPath;

  foreach (it, pathFiles.begin(), pathFiles.end()) {
    KleeHandler::loadPathFile(*it, replayPath);
    replayPaths.push_back(replayPath);
    replayPath.clear();
  }

  Interpreter::InterpreterOptions IOpts;
  IOpts.MakeConcreteSymbolic = MakeConcreteSymbolic;
  KleeHandler *handler = new KleeHandler(InputFile, pArgc, pArgv);
  Interpreter *interpreter = Interpreter::create(IOpts, handler);
  theInterpreter = interpreter;
  handler->setInterpreter(interpreter);
  
  std::ostream &infoFile = handler->getInfoStream();
  for (int i=0; i<argc; i++) {
    infoFile << argv[i] << (i+1<argc ? " ":"\n");
  }

  TwoOStreams info2s(&std::cerr, &infoFile);
  PrefixWriter info(info2s, "KLEE: ");
  info << "PID: " << getpid() << "\n";

  const Module *finalModule = interpreter->setModule(mainModule, Opts);
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
  strftime(buf, sizeof(buf), "Started: %Y-%m-%d %H:%M:%S\n", localtime(&t[0]));
  info << buf;
  infoFile.flush();

  if (!ReplayOutDir.empty() || !ReplayOutFile.empty()) {
    assert(SeedOutFile.empty());
    assert(SeedOutDir.empty());
    runReplay(interpreter, mainFn, pEnvp);
  } else {
    runSeeds(interpreter, mainFn, pArgc, pArgv, pEnvp);
  }
 
  printTimes(info, tms, tm, t);

  // Free all the args.
  for (unsigned i=0; i<InputArgv.size()+1; i++)
    delete[] pArgv[i];
  delete[] pArgv;

  delete interpreter;

  printStats(info, handler);
  delete handler;

  return 0;
}

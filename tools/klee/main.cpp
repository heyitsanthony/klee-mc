/* -*- mode: c++; c-basic-offset: 2; -*- */

#include "klee/Common.h"
#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Interpreter.h"
#include "klee/Statistics.h"
#include "klee/Config/config.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/Internal/ADT/TreeStream.h"
#include "klee/Internal/Support/ModuleUtil.h"
#include "klee/Internal/System/Time.h"

#include "KleeHandler.h"

#include "llvm/Constants.h"
#include "llvm/Module.h"
#include "llvm/ModuleProvider.h" // XXX
#include "llvm/Type.h"
#include "llvm/InstrTypes.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
//#include "llvm/Support/system_error.h"
#include "llvm/Support/TypeBuilder.h"

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

  cl::opt<bool>
  WarnAllExternals("warn-all-externals", 
                   cl::desc("Give initial warning for all externals."));
    
 
 
   
   
    

  enum LibcType {
    NoLibc, KleeLibc, UcLibc
  };

  cl::opt<LibcType>
  Libc("libc", 
       cl::desc("Choose libc version (none by default)."),
       cl::values(clEnumValN(NoLibc, "none", "Don't link in a libc"),
                  clEnumValN(KleeLibc, "klee", "Link in klee libc"),
		  clEnumValN(UcLibc, "uclibc", "Link in uclibc (adapted for klee)"),
		  clEnumValEnd),
       cl::init(NoLibc));

    
  cl::opt<bool>
  WithPOSIXRuntime("posix-runtime", 
		cl::desc("Link with POSIX runtime"),
		cl::init(false));
    
  cl::opt<bool>
  OptimizeModule("optimize", 
                 cl::desc("Optimize before execution"));

  cl::opt<bool>
  CheckDivZero("check-div-zero", 
               cl::desc("Inject checks for division-by-zero"),
               cl::init(true));
    
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
  InitEnv("init-env",
	  cl::desc("Create custom environment.  Options that can be passed as arguments to the programs are: --sym-argv <max-len>  --sym-argvs <min-argvs> <max-argvs> <max-len> + file model options"));

  cl::opt<bool>
  ExcludeLibcCov("exclude-libc-cov",
         cl::desc("Do not track coverage in libc"));

  cl::list<std::string>
  ExcludeCovFiles("exclude-cov-file",
         cl::desc("Filename to load function names to not track coverage for"));
 
  cl::opt<bool>
  Watchdog("watchdog",
           cl::desc("Use a watchdog process to enforce --max-time."),
           cl::init(0));
}

extern bool WriteTraces;
extern cl::opt<double> MaxTime;

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

static void readArgumentsFromFile(char *file, std::vector<std::string> &results) {
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

static void parseArguments(int argc, char **argv) {
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



static int initEnv(Module *mainModule) {

  /*
    nArgcP = alloc oldArgc->getType()
    nArgvV = alloc oldArgv->getType()
    store oldArgc nArgcP
    store oldArgv nArgvP
    klee_init_environment(nArgcP, nArgvP)
    nArgc = load nArgcP
    nArgv = load nArgvP
    oldArgc->replaceAllUsesWith(nArgc)
    oldArgv->replaceAllUsesWith(nArgv)
  */

  Function *baseMainFn = mainModule->getFunction("main");
  assert(baseMainFn);
  baseMainFn->setName("__base_main");

  assert(!baseMainFn->isVarArg() && "main has variable arguments");

  std::vector<const Type*> mainArgs;
  mainArgs.push_back(llvm::TypeBuilder<int,false>::get(getGlobalContext()));
  mainArgs.push_back(llvm::TypeBuilder<char**,false>::get(getGlobalContext()));
  mainArgs.push_back(llvm::TypeBuilder<char**,false>::get(getGlobalContext()));

  // Create a new main() that has standard argc/argv to call the original main
  Function *mainFn =
    Function::Create(FunctionType::get(baseMainFn->getReturnType(), mainArgs,
                                       false),
                     llvm::GlobalValue::ExternalLinkage, "main", mainModule);
  assert(mainFn);

  // set arg names
  llvm::Function::ArgumentListType::iterator argIt =
    mainFn->getArgumentList().begin();
  llvm::Argument *oldArgc, *oldArgv, *oldEnvp;
  oldArgc = &*argIt;
  oldArgv = &*++argIt;
  oldEnvp = &*++argIt;

  oldArgc->setName("argc");
  oldArgv->setName("argv");
  oldEnvp->setName("envp");

  BasicBlock *dBB = BasicBlock::Create(getGlobalContext(), "entry", mainFn);

  AllocaInst* argcPtr = 
    new AllocaInst(oldArgc->getType(), "argcPtr", dBB);
  AllocaInst* argvPtr = 
    new AllocaInst(oldArgv->getType(), "argvPtr", dBB);

  new StoreInst(oldArgc, argcPtr, dBB);
  new StoreInst(oldArgv, argvPtr, dBB);

  /* Insert void klee_init_env(int* argc, char*** argv) */
  std::vector<const Type*> params;
  params.push_back(Type::getInt32Ty(getGlobalContext()));
  params.push_back(Type::getInt32Ty(getGlobalContext()));
  Function* initEnvFn = 
    cast<Function>(mainModule->getOrInsertFunction("klee_init_env",
                                                   Type::getVoidTy(getGlobalContext()),
                                                   argcPtr->getType(),
                                                   argvPtr->getType(),
                                                   NULL));
  assert(initEnvFn);
  std::vector<Value*> args;
  args.push_back(argcPtr);
  args.push_back(argvPtr);
  /*Instruction* initEnvCall = */CallInst::Create(initEnvFn, args.begin(),
                                              args.end(), "", dBB);
  Value *argc = new LoadInst(argcPtr, "newArgc", dBB);
  Value *argv = new LoadInst(argvPtr, "newArgv", dBB);

  std::vector<Value*> baseMainArgs;
  switch(baseMainFn->getFunctionType()->getNumParams()) {
  case 3:
    baseMainArgs.insert(baseMainArgs.begin(),oldEnvp);
  case 2:
    baseMainArgs.insert(baseMainArgs.begin(),argv);
  case 1:
    baseMainArgs.insert(baseMainArgs.begin(),argc);
  case 0:
    break;
  default:
    assert(0 && "Too many arguments to main()");
  }

  CallInst *mainRet = CallInst::Create(baseMainFn, baseMainArgs.begin(),
                                       baseMainArgs.end(), "", dBB);

  ReturnInst::Create(getGlobalContext(), mainRet, dBB);

  return 0;
}

// This is a terrible hack until we get some real modelling of the
// system. All we do is check the undefined symbols and m and warn about
// any "unrecognized" externals and about any obviously unsafe ones.

// Symbols we explicitly support
static const char *modelledExternals[] = {
  "_ZTVN10__cxxabiv117__class_type_infoE",
  "_ZTVN10__cxxabiv120__si_class_type_infoE",
  "_ZTVN10__cxxabiv121__vmi_class_type_infoE",

  // special functions
  "_assert", 
  "__assert_fail", 
  "__assert_rtn", 
  "calloc", 
  "_exit", 
  "exit", 
  "free", 
  "abort", 
  "klee_abort", 
  "klee_assume", 
  "klee_check_memory_access",
  "klee_define_fixed_object",
  "klee_get_errno", 
  "klee_get_value",
  "klee_get_obj_size", 
  "klee_is_symbolic", 
  "klee_make_symbolic", 
  "klee_mark_global", 
  "klee_merge", 
  "klee_prefer_cex",
  "klee_print_expr", 
  "klee_print_range", 
  "klee_report_error", 
  "klee_set_forking",
  "klee_silent_exit", 
  "klee_warning", 
  "klee_warning_once", 
  "klee_alias_function",
  "llvm.dbg.stoppoint", 
  "llvm.va_start", 
  "llvm.va_end", 
  "malloc", 
  "realloc", 
  "_ZdaPv", 
  "_ZdlPv", 
  "_Znaj", 
  "_Znwj", 
  "_Znam", 
  "_Znwm", 
};
// Symbols we aren't going to warn about
static const char *dontCareExternals[] = {
#if 0
  // stdio
  "fprintf",
  "fflush",
  "fopen",
  "fclose",
  "fputs_unlocked",
  "putchar_unlocked",
  "vfprintf",
  "fwrite",
  "puts",
  "printf",
  "stdin",
  "stdout",
  "stderr",
  "_stdio_term",
  "__errno_location",
  "fstat",
#endif

  // static information, pretty ok to return
  "getegid",
  "geteuid",
  "getgid",
  "getuid",
  "getpid",
  "gethostname",
  "getpgrp",
  "getppid",
  "getpagesize",
  "getpriority",
  "getgroups",
  "getdtablesize",
  "getrlimit",
  "getrlimit64",
  "getcwd",
  "getwd",
  "gettimeofday",
  "uname",

  // fp stuff we just don't worry about yet
  "frexp",  
  "ldexp",
  "__isnan",
  "__signbit",
};
// Extra symbols we aren't going to warn about with klee-libc
static const char *dontCareKlee[] = {
  "__ctype_b_loc",
  "__ctype_get_mb_cur_max",

  // io system calls
  "open",
  "write",
  "read",
  "close",
};
// Extra symbols we aren't going to warn about with uclibc
static const char *dontCareUclibc[] = {
  "__dso_handle",

  // Don't warn about these since we explicitly commented them out of
  // uclibc.
  "printf",
  "vprintf"
};
// Symbols we consider unsafe
static const char *unsafeExternals[] = {
  "fork", // oh lord
  "exec", // heaven help us
  "error", // calls _exit
  "raise", // yeah
  "kill", // mmmhmmm
};
#define NELEMS(array) (sizeof(array)/sizeof(array[0]))
void externalsAndGlobalsCheck(const Module *m) {
  std::map<std::string, bool> externals;
  std::set<std::string> modelled(modelledExternals, 
                                 modelledExternals+NELEMS(modelledExternals));
  std::set<std::string> dontCare(dontCareExternals, 
                                 dontCareExternals+NELEMS(dontCareExternals));
  std::set<std::string> unsafe(unsafeExternals, 
                               unsafeExternals+NELEMS(unsafeExternals));

  switch (Libc) {
  case KleeLibc: 
    dontCare.insert(dontCareKlee, dontCareKlee+NELEMS(dontCareKlee));
    break;
  case UcLibc:
    dontCare.insert(dontCareUclibc,
                    dontCareUclibc+NELEMS(dontCareUclibc));
    break;
  case NoLibc: /* silence compiler warning */
    break;
  }
  

  if (WithPOSIXRuntime)
    dontCare.insert("syscall");

  for (Module::const_iterator fnIt = m->begin(), fn_ie = m->end(); 
       fnIt != fn_ie; ++fnIt) {
    if (fnIt->isDeclaration() && !fnIt->use_empty())
      externals.insert(std::make_pair(fnIt->getName(), false));
    for (Function::const_iterator bbIt = fnIt->begin(), bb_ie = fnIt->end(); 
         bbIt != bb_ie; ++bbIt) {
      for (BasicBlock::const_iterator it = bbIt->begin(), ie = bbIt->end(); 
           it != ie; ++it) {
        if (const CallInst *ci = dyn_cast<CallInst>(it)) {
          if (isa<InlineAsm>(ci->getCalledValue())) {
            klee_warning_once(&*fnIt,
                              "function \"%s\" has inline asm", 
                              fnIt->getName().data());
          }
        }
      }
    }
  }
  for (Module::const_global_iterator 
         it = m->global_begin(), ie = m->global_end(); 
       it != ie; ++it)
    if (it->isDeclaration() && !it->use_empty())
      externals.insert(std::make_pair(it->getName(), true));
  // and remove aliases (they define the symbol after global
  // initialization)
  for (Module::const_alias_iterator 
         it = m->alias_begin(), ie = m->alias_end(); 
       it != ie; ++it) {
    std::map<std::string, bool>::iterator it2 = 
      externals.find(it->getName());
    if (it2!=externals.end())
      externals.erase(it2);
  }

  std::map<std::string, bool> foundUnsafe;
  for (std::map<std::string, bool>::iterator
         it = externals.begin(), ie = externals.end();
       it != ie; ++it) {
    const std::string &ext = it->first;
    if (!modelled.count(ext) && (WarnAllExternals || 
                                 !dontCare.count(ext))) {
      if (unsafe.count(ext)) {
        foundUnsafe.insert(*it);
      } else {
        klee_warning("undefined reference to %s: %s",
                     it->second ? "variable" : "function",
                     ext.c_str());
      }
    }
  }

  for (std::map<std::string, bool>::iterator
         it = foundUnsafe.begin(), ie = foundUnsafe.end();
       it != ie; ++it) {
    const std::string &ext = it->first;
    klee_warning("undefined reference to %s: %s (UNSAFE)!",
                 it->second ? "variable" : "function",
                 ext.c_str());
  }
}

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

#ifndef KLEE_UCLIBC
static llvm::Module *linkWithUclibc(llvm::Module *mainModule) {
  fprintf(stderr, "error: invalid libc, no uclibc support!\n");
  exit(1);
  return 0;
}
#else

static void uclibc_forceImports(llvm::Module* mainModule)
{
    const llvm::Type *i8Ty = Type::getInt8Ty(getGlobalContext());
    mainModule->getOrInsertFunction("realpath",
                                    PointerType::getUnqual(i8Ty),
                                    PointerType::getUnqual(i8Ty),
                                    PointerType::getUnqual(i8Ty),
                                    NULL);
    mainModule->getOrInsertFunction("getutent",
                                    PointerType::getUnqual(i8Ty),
                                    NULL);
    mainModule->getOrInsertFunction("__fgetc_unlocked",
                                    Type::getInt32Ty(getGlobalContext()),
                                    PointerType::getUnqual(i8Ty),
                                    NULL);
    mainModule->getOrInsertFunction("__fputc_unlocked",
                                    Type::getInt32Ty(getGlobalContext()),
                                    Type::getInt32Ty(getGlobalContext()),
                                    PointerType::getUnqual(i8Ty),
                                    NULL);
}

static void uclibc_stripPrefixes(llvm::Module* mainModule)
{
  for (Module::iterator fi = mainModule->begin(), fe = mainModule->end();
       fi != fe; fi++) {
    Function *f = fi;
    const std::string &name = f->getName();
    unsigned size = name.size();

    if (name[0] !='\01') continue;
    if (name[size-2] !='6' || name[size-1] !='4') continue;
    std::string unprefixed = name.substr(1);

    // See if the unprefixed version exists.
    if (Function *f2 = mainModule->getFunction(unprefixed)) {
       f->replaceAllUsesWith(f2);
       f->eraseFromParent();
    } else {
      f->setName(unprefixed);
    }
  }
}

static void uclibc_fixups(llvm::Module* mainModule)
{
  // more sighs, this is horrible but just a temp hack
  //    f = mainModule->getFunction("__fputc_unlocked");
  //    if (f) f->setName("fputc_unlocked");
  //    f = mainModule->getFunction("__fgetc_unlocked");
  //    if (f) f->setName("fgetc_unlocked");
  
  Function *f, *f2;
  f = mainModule->getFunction("open");
  f2 = mainModule->getFunction("__libc_open");
  if (f2) {
    if (f) {
      f2->replaceAllUsesWith(f);
      f2->eraseFromParent();
    } else {
      f2->setName("open");
      assert(f2->getName() == "open");
    }
  }

  f = mainModule->getFunction("fcntl");
  f2 = mainModule->getFunction("__libc_fcntl");
  if (f2) {
    if (f) {
      f2->replaceAllUsesWith(f);
      f2->eraseFromParent();
    } else {
      f2->setName("fcntl");
      assert(f2->getName() == "fcntl");
    }
  }
}

static void uclibc_setEntry(llvm::Module* mainModule)
{
  Function *userMainFn = mainModule->getFunction("main");
  assert(userMainFn && "unable to get user main");    
  Function *uclibcMainFn = mainModule->getFunction("__uClibc_main");
  assert(uclibcMainFn && "unable to get uclibc main");    
  userMainFn->setName("__user_main");

  const FunctionType *ft = uclibcMainFn->getFunctionType();
  assert(ft->getNumParams() == 7);

  std::vector<const Type*> fArgs;
  fArgs.push_back(ft->getParamType(1)); // argc
  fArgs.push_back(ft->getParamType(2)); // argv
  Function *stub = Function::Create(
    FunctionType::get(
      Type::getInt32Ty(getGlobalContext()), fArgs, false),
      GlobalVariable::ExternalLinkage,
      "main",
      mainModule);
  BasicBlock *bb = BasicBlock::Create(getGlobalContext(), "entry", stub);

  std::vector<llvm::Value*> args;
  args.push_back(llvm::ConstantExpr::getBitCast(
    userMainFn, ft->getParamType(0)));
  args.push_back(stub->arg_begin()); // argc
  args.push_back(++stub->arg_begin()); // argv    
  args.push_back(Constant::getNullValue(ft->getParamType(3))); // app_init
  args.push_back(Constant::getNullValue(ft->getParamType(4))); // app_fini
  args.push_back(Constant::getNullValue(ft->getParamType(5))); // rtld_fini
  args.push_back(Constant::getNullValue(ft->getParamType(6))); // stack_end
  CallInst::Create(uclibcMainFn, args.begin(), args.end(), "", bb);
  
  new UnreachableInst(getGlobalContext(), bb);
}

static llvm::Module *linkWithUclibc(llvm::Module *mainModule)
{
  Function *f;
  // force import of __uClibc_main
  mainModule->getOrInsertFunction(
    "__uClibc_main",
    FunctionType::get(Type::getVoidTy(getGlobalContext()),
    std::vector<const Type*>(),
    true));
  
  // force various imports
  if (WithPOSIXRuntime) uclibc_forceImports(mainModule);

  f = mainModule->getFunction("__ctype_get_mb_cur_max");
  if (f) f->setName("_stdlib_mb_cur_max");

  // Strip of asm prefixes for 64 bit versions because they are not
  // present in uclibc and we want to make sure stuff will get
  // linked. In the off chance that both prefixed and unprefixed
  // versions are present in the module, make sure we don't create a
  // naming conflict.
  uclibc_stripPrefixes(mainModule);


  mainModule = klee::linkWithLibrary(mainModule, KLEE_UCLIBC "/lib/libc.a");
  assert(mainModule && "unable to link with uclibc");

  uclibc_fixups(mainModule);

  // XXX we need to rearchitect so this can also be used with
  // programs externally linked with uclibc.

  // We now need to swap things so that __uClibc_main is the entry
  // point, in such a way that the arguments are passed to
  // __uClibc_main correctly. We do this by renaming the user main
  // and generating a stub function to call __uClibc_main. There is
  // also an implicit cooperation in that runFunctionAsMain sets up
  // the environment arguments to what uclibc expects (following
  // argv), since it does not explicitly take an envp argument.
  uclibc_setEntry(mainModule);
  return mainModule;
}
#endif

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

  pbuf += sprintf(pbuf = buf, "Elapsed: ");
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

static Module* setupLibc(Module* mainModule, Interpreter::ModuleOptions& Opts)
{
  switch (Libc) {
  case NoLibc: /* silence compiler warning */
    break;

  case KleeLibc: {
    // FIXME: Find a reasonable solution for this.
    llvm::sys::Path Path(Opts.LibraryDir);
    Path.appendComponent("libklee-libc.bca");
    mainModule = klee::linkWithLibrary(mainModule, Path.c_str());
    assert(mainModule && "unable to link with klee-libc");
    if(ExcludeLibcCov) {
      llvm::sys::Path ExcludePath(Opts.LibraryDir);
      ExcludePath.appendComponent("klee-libc-fns.txt");
      Opts.ExcludeCovFiles.push_back(ExcludePath.c_str());
    }
    break;
  }

  case UcLibc:
    mainModule = linkWithUclibc(mainModule);
    if(ExcludeLibcCov) {
      llvm::sys::Path ExcludePath(Opts.LibraryDir);
      ExcludePath.appendComponent("uclibc-fns.txt");
      Opts.ExcludeCovFiles.push_back(ExcludePath.c_str());
    }
    break;
  }

  return mainModule;
}

int main(int argc, char **argv, char **envp) {  
#if ENABLE_STPLOG == 1
  STPLOG_init("stplog.c");
#endif

  atexit(llvm_shutdown);  // Call llvm_shutdown() on exit.

  llvm::InitializeNativeTarget();

  parseArguments(argc, argv);
  sys::PrintStackTraceOnErrorSignal();

  if (Watchdog) return runWatchdog();

  sys::SetInterruptFunction(interrupt_handle);

  // Load the bytecode...
  std::string ErrorMsg;
#if 0  
  Module *mainModule = 0;
  OwningPtr<MemoryBuffer> Buffer;
  MemoryBuffer::getFileOrSTDIN(InputFile, Buffer);
  if (Buffer) {
    mainModule = getLazyBitcodeModule(Buffer.get(), getGlobalContext(), &ErrorMsg);
    if (!mainModule) Buffer.reset();
  }
  if (mainModule) {
    if (mainModule->MaterializeAllPermanently(&ErrorMsg)) {
      delete mainModule;
      mainModule = 0;
   }
  }            

  if (!mainModule)
    klee_error("error loading program '%s': %s",
    	InputFile.c_str(), ErrorMsg.c_str());

  assert(mainModule && "unable to materialize");
#else
  ModuleProvider *MP = 0;
  if (MemoryBuffer *Buffer = MemoryBuffer::getFileOrSTDIN(InputFile, &ErrorMsg)) {
    MP = getBitcodeModuleProvider(Buffer, getGlobalContext(), &ErrorMsg);
    if (!MP) delete Buffer;
  }
  
  if (!MP)
    klee_error("error loading program '%s': %s", InputFile.c_str(), ErrorMsg.c_str());

  Module *mainModule = MP->materializeModule();
  MP->releaseModule();
  delete MP;

  assert(mainModule && "unable to materialize");


#endif
  // Remove '\x01' prefix sentinels before linking
  runRemoveSentinelsPass(*mainModule);
  
  if (WithPOSIXRuntime) InitEnv = true;

  if (InitEnv) {
    int r = initEnv(mainModule);
    if (r != 0)
      return r;
  }

  llvm::sys::Path LibraryDir(KLEE_DIR "/" RUNTIME_CONFIGURATION "/lib");
  Interpreter::ModuleOptions Opts(LibraryDir.c_str(),
                                  /*Optimize=*/OptimizeModule, 
                                  /*CheckDivZero=*/CheckDivZero,
                                  ExcludeCovFiles);
  
  mainModule = setupLibc(mainModule, Opts);

  if(ExcludeLibcCov) {
    llvm::sys::Path ExcludePath(Opts.LibraryDir);
    ExcludePath.appendComponent("kleeRuntimeIntrinsic-fns.txt");
    Opts.ExcludeCovFiles.push_back(ExcludePath.c_str());
  }

  if (WithPOSIXRuntime) {
    llvm::sys::Path Path(Opts.LibraryDir);
    Path.appendComponent("libkleeRuntimePOSIX.bca");
    klee_message("NOTE: Using model: %s", Path.c_str());
    mainModule = klee::linkWithLibrary(mainModule, Path.c_str());
    assert(mainModule && "unable to link with simple model");
    if(ExcludeLibcCov) {
      llvm::sys::Path ExcludePath(Opts.LibraryDir);
      ExcludePath.appendComponent("kleeRuntimePOSIX-fns.txt");
      Opts.ExcludeCovFiles.push_back(ExcludePath.c_str());
    }
  }  

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
  if (ReplayPathDir != "")
    KleeHandler::getPathFiles(ReplayPathDir, pathFiles);
  if (ReplayPathFile != "")
    pathFiles.push_back(ReplayPathFile);

  std::list<Interpreter::ReplayPathType> replayPaths;
  Interpreter::ReplayPathType replayPath;

  for(std::vector<std::string>::iterator it = pathFiles.begin();
      it != pathFiles.end(); ++it) {
    KleeHandler::loadPathFile(*it, replayPath);
    replayPaths.push_back(replayPath);
    replayPath.clear();
  }

  Interpreter::InterpreterOptions IOpts;
  IOpts.MakeConcreteSymbolic = MakeConcreteSymbolic;
  KleeHandler *handler = new KleeHandler(InputFile, pArgc, pArgv);
  Interpreter *interpreter = 
    theInterpreter = Interpreter::create(IOpts, handler);
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

  char buf[256], *pbuf;
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

    std::vector<std::string> outFiles = ReplayOutFile;
    for (std::vector<std::string>::iterator
           it = ReplayOutDir.begin(), ie = ReplayOutDir.end();
         it != ie; ++it)
      KleeHandler::getOutFiles(*it, outFiles);    
    std::vector<KTest*> kTests;
    for (std::vector<std::string>::iterator
           it = outFiles.begin(), ie = outFiles.end();
         it != ie; ++it) {
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
    for (std::vector<KTest*>::iterator
           it = kTests.begin(), ie = kTests.end();
         it != ie; ++it) {
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
  } else {
    std::vector<KTest *> seeds;
    for (std::vector<std::string>::iterator
           it = SeedOutFile.begin(), ie = SeedOutFile.end();
         it != ie; ++it) {
      KTest *out = kTest_fromFile(it->c_str());
      if (!out) {
        std::cerr << "KLEE: unable to open: " << *it << "\n";
        exit(1);
      }
      seeds.push_back(out);
    } 
    for (std::vector<std::string>::iterator
           it = SeedOutDir.begin(), ie = SeedOutDir.end();
         it != ie; ++it) {
      std::vector<std::string> outFiles;
      KleeHandler::getOutFiles(*it, outFiles);
      for (std::vector<std::string>::iterator
             it2 = outFiles.begin(), ie = outFiles.end();
           it2 != ie; ++it2) {
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

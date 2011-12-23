#include "klee/Common.h"
#include "klee/Interpreter.h"
#include "klee/Internal/Support/ModuleUtil.h"
#include <llvm/Constants.h>
#include "llvm/Module.h"
#include "llvm/Instructions.h"

#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/system_error.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/TypeBuilder.h"
#include "llvm/Support/Signals.h"

#include "static/Sugar.h"
#include "libc.h"

#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include "klee/Config/config.h"

#include <iostream>
#include <set>
#include <map>

using namespace klee;
using namespace llvm;

namespace {
  cl::opt<bool> OptimizeModule(
    "optimize",
    cl::desc("Optimize before execution"));

  cl::opt<bool> InitEnv(
    "init-env",
	  cl::desc("Create custom environment.  Options that can be passed as arguments to the programs are: --sym-argv <max-len>  --sym-argvs <min-argvs> <max-argvs> <max-len> + file model options"));


  cl::opt<bool> ExcludeLibcCov(
    "exclude-libc-cov",
    cl::desc("Do not track coverage in libc"));

  cl::list<std::string> ExcludeCovFiles(
    "exclude-cov-file",
    cl::desc("Filename to load function names to not track coverage for"));
}


//extern cl::opt<LibcType> g_Libc;
//
extern std::string g_InputFile;
extern LibcType g_Libc;
extern bool g_WithPOSIXRuntime;

namespace
{
  cl::opt<bool> WarnAllExternals(
    "warn-all-externals",
    cl::desc("Give initial warning for all externals."));
}

// Symbols we explicitly support
static const char *modelledExternals[] =
{
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
  "klee_stack_trace",
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
void externalsAndGlobalsCheck(const Module *m)
{
  std::map<std::string, bool> externals;
  std::set<std::string> modelled(modelledExternals,
                                 modelledExternals+NELEMS(modelledExternals));
  std::set<std::string> dontCare(dontCareExternals,
                                 dontCareExternals+NELEMS(dontCareExternals));
  std::set<std::string> unsafe(unsafeExternals,
                               unsafeExternals+NELEMS(unsafeExternals));

  switch (g_Libc) {
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


  if (g_WithPOSIXRuntime) dontCare.insert("syscall");

  foreach (fnIt, m->begin(), m->end()) {
    if (fnIt->isDeclaration() && !fnIt->use_empty())
      externals.insert(std::make_pair(fnIt->getName(), false));
    foreach (bbIt, fnIt->begin(), fnIt->end()) {
      foreach (it, bbIt->begin(),  bbIt->end()) {
        const CallInst *ci = dyn_cast<CallInst>(it);
        if (!ci) continue;
        if (!isa<InlineAsm>(ci->getCalledValue())) continue;
        klee_warning_once(&*fnIt,
                          "function \"%s\" has inline asm",
                          fnIt->getName().data());
      }
    }
  }

  foreach (it, m->global_begin(), m->global_end()) {
    if (!it->isDeclaration() || it->use_empty()) continue;
    externals.insert(std::make_pair(it->getName(), true));
  }

  // and remove aliases (they define the symbol after global
  // initialization)
  foreach (it, m->alias_begin(), m->alias_end()) {
    std::map<std::string, bool>::iterator it2 = externals.find(it->getName());
    if (it2 == externals.end()) continue;
    externals.erase(it2);
  }

  std::map<std::string, bool> foundUnsafe;
  foreach (it, externals.begin(), externals.end()) {
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

  foreach (it, foundUnsafe.begin(), foundUnsafe.end()) {
    const std::string &ext = it->first;
    klee_warning("undefined reference to %s: %s (UNSAFE)!",
                 it->second ? "variable" : "function",
                 ext.c_str());
  }
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
    llvm::Type *i8Ty = Type::getInt8Ty(getGlobalContext());
    mainModule->getOrInsertFunction(
      "realpath",
      PointerType::getUnqual(i8Ty),
      PointerType::getUnqual(i8Ty),
      PointerType::getUnqual(i8Ty),
      NULL);
    mainModule->getOrInsertFunction(
      "getutent",
      PointerType::getUnqual(i8Ty),
      NULL);
    mainModule->getOrInsertFunction(
      "__fgetc_unlocked",
      Type::getInt32Ty(getGlobalContext()),
      PointerType::getUnqual(i8Ty),
      NULL);
    mainModule->getOrInsertFunction(
      "__fputc_unlocked",
      Type::getInt32Ty(getGlobalContext()),
      Type::getInt32Ty(getGlobalContext()),
      PointerType::getUnqual(i8Ty),
      NULL);
}

static void uclibc_stripPrefixes(llvm::Module* mainModule)
{
  foreach (fi, mainModule->begin(), mainModule->end()) {
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

  FunctionType *ft = uclibcMainFn->getFunctionType();
  assert(ft->getNumParams() == 7);

  std::vector<Type*> fArgs;
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
  CallInst::Create(uclibcMainFn, args, "", bb);

  new UnreachableInst(getGlobalContext(), bb);
}

static llvm::Module *linkWithUclibc(llvm::Module *mainModule)
{
  Function *f;
  // force import of __uClibc_main
  mainModule->getOrInsertFunction(
    "__uClibc_main",
    FunctionType::get(Type::getVoidTy(getGlobalContext()),
    std::vector<Type*>(),
    true));

  // force various imports
  if (g_WithPOSIXRuntime) uclibc_forceImports(mainModule);

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


static Module* setupLibc(Module* mainModule, Interpreter::ModuleOptions& Opts)
{
  switch (g_Libc) {
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


static int initEnv(Module *mainModule)
{
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

  std::vector<Type*> mainArgs;
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
  std::vector<Type*> params;
  params.push_back(Type::getInt32Ty(getGlobalContext()));
  params.push_back(Type::getInt32Ty(getGlobalContext()));
  Function* initEnvFn =
    cast<Function>(mainModule->getOrInsertFunction(
      "klee_init_env",
      Type::getVoidTy(getGlobalContext()),
      argcPtr->getType(),
      argvPtr->getType(),
      NULL));

  assert(initEnvFn);
  std::vector<Value*> args;
  args.push_back(argcPtr);
  args.push_back(argvPtr);
  /*Instruction* initEnvCall = */
  CallInst::Create(initEnvFn, args, "", dBB);
  Value *argc = new LoadInst(argcPtr, "newArgc", dBB);
  Value *argv = new LoadInst(argvPtr, "newArgv", dBB);

  std::vector<Value*> baseMainArgs;
  switch(baseMainFn->getFunctionType()->getNumParams()) {
  case 3: baseMainArgs.insert(baseMainArgs.begin(),oldEnvp);
  case 2: baseMainArgs.insert(baseMainArgs.begin(),argv);
  case 1: baseMainArgs.insert(baseMainArgs.begin(),argc);
  case 0: break;
  default:
    assert(0 && "Too many arguments to main()");
  }

  CallInst *mainRet = CallInst::Create(baseMainFn, baseMainArgs, "", dBB);

  ReturnInst::Create(getGlobalContext(), mainRet, dBB);

  return 0;
}


Interpreter::ModuleOptions getMainModule(Module* &mainModule)
{
	std::string ErrorMsg;
	OwningPtr<MemoryBuffer> Buffer;
	MemoryBuffer::getFileOrSTDIN(g_InputFile.c_str(), Buffer);

	mainModule = 0;
	if (Buffer) {
		//mainModule = getLazyBitcodeModule(Buffer.get(), getGlobalContext(), &ErrorMsg);
		mainModule = ParseBitcodeFile(Buffer.get(), getGlobalContext(), &ErrorMsg);
		if (!mainModule) Buffer.reset();
	}

	if (mainModule) {
		if (mainModule->MaterializeAllPermanently(&ErrorMsg)) {
			delete mainModule;
			mainModule = 0;
		}
	}

	if (!mainModule)
		klee_error(
			"error loading program '%s': %s",
			g_InputFile.c_str(),
			ErrorMsg.c_str());

	assert(mainModule && "unable to materialize");

	// Remove '\x01' prefix sentinels before linking
	runRemoveSentinelsPass(*mainModule);

	if (g_WithPOSIXRuntime) InitEnv = true;

	if (InitEnv) {
		int r = initEnv(mainModule);
		if (r != 0) exit(r);
	}

	llvm::sys::Path LibraryDir(KLEE_DIR "/" RUNTIME_CONFIGURATION "/lib");
	Interpreter::ModuleOptions Opts(
		LibraryDir.c_str(),
		OptimizeModule,
		false,
		ExcludeCovFiles);

	mainModule = setupLibc(mainModule, Opts);

	if(ExcludeLibcCov) {
		llvm::sys::Path ExcludePath(Opts.LibraryDir);
		ExcludePath.appendComponent("kleeRuntimeIntrinsic-fns.txt");
		Opts.ExcludeCovFiles.push_back(ExcludePath.c_str());
	}

	if (g_WithPOSIXRuntime) {
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

	return Opts;
}

#include "klee/Common.h"
#include "klee/Interpreter.h"
#include "klee/Internal/Support/ModuleUtil.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/TypeBuilder.h>
#include <llvm/IRReader/IRReader.h>

#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/SourceMgr.h>

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

#define GCTX	getGlobalContext()

namespace {
  cl::opt<bool> InitEnv(
	"init-env",
	cl::desc("Create custom environment.  Options that can be passed as arguments to the programs are: --sym-argv <max-len>  --sym-argvs <min-argvs> <max-argvs> <max-len> + file model options"));


  cl::opt<bool> ExcludeLibcCov(
  	"exclude-libc-cov", cl::desc("Do not track coverage in libc"));

  cl::list<std::string> ExcludeCovFiles(
	"exclude-cov-file",
	cl::desc("Filename to load function names to not track coverage for"));

  cl::opt<bool> WarnAllExternals(
	"warn-all-externals",
	cl::desc("Give initial warning for all externals."));
}


//extern cl::opt<LibcType> g_Libc;
//
extern std::string g_InputFile;
extern LibcType g_Libc;
extern bool g_WithPOSIXRuntime;

#include "mod_symbols.h"

#define NELEMS(array) (sizeof(array)/sizeof(array[0]))

static void checkUndefined(
	const std::map<std::string, bool>& externals,
	const std::set<std::string>& modelled,
	const std::set<std::string>& dontCare,
	const std::set<std::string>& unsafe)
{
	std::map<std::string, bool> foundUnsafe;

	foreach (it, externals.begin(), externals.end()) {
		const std::string &ext = it->first;
		if (modelled.count(ext))
			continue;

		if (!WarnAllExternals && dontCare.count(ext))
			continue;

		if (unsafe.count(ext)) {
			foundUnsafe.insert(*it);
			continue;

		}
		klee_warning("undefined reference to %s: %s",
		     it->second ? "variable" : "function",
		     ext.c_str());
	}


	foreach (it, foundUnsafe.begin(), foundUnsafe.end()) {
		const std::string &ext = it->first;
		klee_warning("undefined reference to %s: %s (UNSAFE)!",
			 it->second ? "variable" : "function",
			 ext.c_str());
	}
}

static void checkInlineAsm(const llvm::Function *f)
{
	foreach (bbIt, f->begin(), f->end()) {
		foreach (it, bbIt->begin(),  bbIt->end()) {
			const CallInst *ci = dyn_cast<CallInst>(it);

			if (!ci) continue;

			if (!isa<InlineAsm>(ci->getCalledValue()))
				continue;

			klee_warning_once(
				f,
				"function \"%s\" has inline asm",
				f->getName().data());
		}
	}
}

#define DECL_EXTERNS(x)	\
std::set<std::string> x(x##Externals, x##Externals+NELEMS(x##Externals))

void externalsAndGlobalsCheck(const Module *m)
{
	std::map<std::string, bool> externals;
	DECL_EXTERNS(modelled);
	DECL_EXTERNS(dontCare);
	DECL_EXTERNS(unsafe);

	switch (g_Libc) {
	case KleeLibc:
		dontCare.insert(
			dontCareKlee,
			dontCareKlee+NELEMS(dontCareKlee));
		break;
	case UcLibc:
		dontCare.insert(
			dontCareUclibc,
			dontCareUclibc+NELEMS(dontCareUclibc));
		break;
	case NoLibc: /* silence compiler warning */
		break;
	}

	if (g_WithPOSIXRuntime) dontCare.insert("syscall");

	foreach (fnIt, m->begin(), m->end()) {
		/* XXX: should look at specialfunctionhandler to check
		 * whether function is a klee intrinsic */
		if (fnIt->isDeclaration() && !fnIt->use_empty()) {
			externals.insert(
				std::make_pair(fnIt->getName(), false));
		}

		checkInlineAsm(fnIt);

	}

	foreach (it, m->global_begin(), m->global_end()) {
		if (!it->isDeclaration() || it->use_empty())
			continue;
		externals.insert(std::make_pair(it->getName(), true));
	}

	// and remove aliases
	// (they define the symbol after global initialization)
	foreach (it, m->alias_begin(), m->alias_end()) {
		std::map<std::string, bool>::iterator it2;

		it2 = externals.find(it->getName());
		if (it2 == externals.end()) continue;
		std::cerr << "ERASING " << it2->first << '\n';
		externals.erase(it2);
	}

	checkUndefined(externals, modelled, dontCare, unsafe);
}

#ifndef KLEE_UCLIBC
static llvm::Module *linkWithUclibc(llvm::Module *mainModule)
{
	fprintf(stderr, "error: invalid libc, no uclibc support!\n");
	exit(1);
	return 0;
}
#else

static void uclibc_forceImports(llvm::Module* mainModule)
{
	llvm::Type *i8Ty = Type::getInt8Ty(GCTX);
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
		Type::getInt32Ty(GCTX),
		PointerType::getUnqual(i8Ty),
		NULL);
	mainModule->getOrInsertFunction(
		"__fputc_unlocked",
		Type::getInt32Ty(GCTX),
		Type::getInt32Ty(GCTX),
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

static void fixup_func(llvm::Module* m, const char *dest, const char *src)
{
	Function *f, *f2;

	f = m->getFunction(dest);
	f2 = m->getFunction(src);

	if (f2 == NULL) return;

	if (f) {
		f2->replaceAllUsesWith(f);
		f2->eraseFromParent();
	} else {
		f2->setName(dest);
		assert(f2->getName() == dest);
	}
}

static void uclibc_fixups(llvm::Module* mainModule)
{
	// more sighs, this is horrible but just a temp hack
	//    f = mainModule->getFunction("__fputc_unlocked");
	//    if (f) f->setName("fputc_unlocked");
	//    f = mainModule->getFunction("__fgetc_unlocked");
	//    if (f) f->setName("fgetc_unlocked");

	fixup_func(mainModule, "open", "__libc_open");
	fixup_func(mainModule, "fcntl", "__libc_fcntl");
}

static void uclibc_setEntry(llvm::Module* mainModule)
{
	FunctionType		*ft;
	Function		*userMainFn, *uclibcMainFn, *stub;
	BasicBlock		*bb;
	std::vector<Type*>	fArgs;
	std::vector<llvm::Value*> args;

	userMainFn = mainModule->getFunction("main");
	assert(userMainFn && "unable to get user main");

	uclibcMainFn = mainModule->getFunction("__uClibc_main");
	assert(uclibcMainFn && "unable to get uclibc main");

	userMainFn->setName("__user_main");

	ft = uclibcMainFn->getFunctionType();
	assert(ft->getNumParams() == 7);

	/* argc, argv */
	fArgs.push_back(ft->getParamType(1));
	fArgs.push_back(ft->getParamType(2));
	stub = Function::Create(
		FunctionType::get(
			Type::getInt32Ty(GCTX), fArgs, false),
			GlobalVariable::ExternalLinkage,
			"main",
			mainModule);

	bb = BasicBlock::Create(GCTX, "entry", stub);

	/* mainPtr, argc, argv, app_init, app_fini, rtld_fini, stack_end */
	args.push_back(
		llvm::ConstantExpr::getBitCast(
			userMainFn,
			ft->getParamType(0)));
	args.push_back(stub->arg_begin());
	args.push_back(++stub->arg_begin());
	args.push_back(Constant::getNullValue(ft->getParamType(3)));
	args.push_back(Constant::getNullValue(ft->getParamType(4)));
	args.push_back(Constant::getNullValue(ft->getParamType(5)));
	args.push_back(Constant::getNullValue(ft->getParamType(6)));
	CallInst::Create(uclibcMainFn, args, "", bb);

	new UnreachableInst(GCTX, bb);
}

static llvm::Module *linkWithUclibc(llvm::Module *mainModule)
{
	Function	*f;

	// force import of __uClibc_main
	mainModule->getOrInsertFunction(
		"__uClibc_main",
		FunctionType::get(Type::getVoidTy(GCTX),
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

	mainModule = klee::linkWithLibrary(mainModule, KLEE_UCLIBC "/lib/libc.bc");
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


static Module* setupLibc(Module* mainModule, ModuleOptions& Opts)
{
	const char *exclude_fns_f = NULL;

	switch (g_Libc) {
	/* silence compiler warning */
	case NoLibc: break;

	case KleeLibc: {
		// FIXME: Find a reasonable solution for this.
		// XXX SOLUTION FOR WHAT!? --AJR
		std::string	Path(Opts.LibraryDir);
		Path = Path + "/libklee-libc.bc";
		mainModule = klee::linkWithLibrary(mainModule, Path.c_str());
		assert(mainModule && "unable to link with klee-libc");

		if (ExcludeLibcCov) exclude_fns_f = "klee-libc-fns.txt";
		break;
	}

	case UcLibc:
		mainModule = linkWithUclibc(mainModule);
		if (ExcludeLibcCov) exclude_fns_f = "uclibc-fns.txt";
		break;
	}


	if (exclude_fns_f != NULL) {
		std::string	ExcludePath(Opts.LibraryDir);
		ExcludePath = ExcludePath + "/" + exclude_fns_f;
		Opts.ExcludeCovFiles.push_back(ExcludePath.c_str());
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
  mainArgs.push_back(llvm::TypeBuilder<int,false>::get(GCTX));
  mainArgs.push_back(llvm::TypeBuilder<char**,false>::get(GCTX));
  mainArgs.push_back(llvm::TypeBuilder<char**,false>::get(GCTX));

  // Create a new main() that has standard argc/argv to call the original main
  Function *mainFn =
    Function::Create(
    	FunctionType::get(baseMainFn->getReturnType(), mainArgs, false),
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

  BasicBlock *dBB = BasicBlock::Create(GCTX, "entry", mainFn);

  AllocaInst* argcPtr = new AllocaInst(oldArgc->getType(), "argcPtr", dBB);
  AllocaInst* argvPtr = new AllocaInst(oldArgv->getType(), "argvPtr", dBB);

  new StoreInst(oldArgc, argcPtr, dBB);
  new StoreInst(oldArgv, argvPtr, dBB);

  /* Insert void klee_init_env(int* argc, char*** argv) */
  std::vector<Type*> params;
  params.push_back(Type::getInt32Ty(GCTX));
  params.push_back(Type::getInt32Ty(GCTX));
  Function* initEnvFn =
    cast<Function>(mainModule->getOrInsertFunction(
      "klee_init_env",
      Type::getVoidTy(GCTX),
      argcPtr->getType(), argvPtr->getType(), NULL));

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

  ReturnInst::Create(GCTX, mainRet, dBB);

  return 0;
}

static void loadIntrinsics(ModuleOptions& Opts)
{
	if (!ExcludeLibcCov) return;
	std::string	ExcludePath(Opts.LibraryDir);
	ExcludePath = ExcludePath + "/kleeRuntimeIntrinsic-fns.txt";
	Opts.ExcludeCovFiles.push_back(ExcludePath.c_str());
}

static void loadPOSIX(Module* &mainModule, ModuleOptions& Opts)
{
	if (!g_WithPOSIXRuntime) return;

	std::string Path(Opts.LibraryDir);
	Path = Path + ("/libkleeRuntimePOSIX.bc");
	klee_message("NOTE: Using model: %s", Path.c_str());

	mainModule = klee::linkWithLibrary(mainModule, Path.c_str());
	assert(mainModule && "unable to link with simple model");
	if (ExcludeLibcCov) {
		std::string	ExcludePath(Opts.LibraryDir);
		ExcludePath = ExcludePath + "/kleeRuntimePOSIX-fns.txt";
		Opts.ExcludeCovFiles.push_back(ExcludePath.c_str());
	}
}

void loadInputBitcode(const std::string& ifile, Module* &mainModule)
{
	SMDiagnostic	diag;
	auto Buffer(MemoryBuffer::getFileOrSTDIN(ifile.c_str()));

	mainModule = NULL;
	if (!Buffer) goto done;

	mainModule = llvm::ParseIR(Buffer.get().get(), diag, GCTX);
	if (mainModule == NULL) {
		goto done;
	}

	if (mainModule->materializeAllPermanently()) {
		delete mainModule;
		mainModule = NULL;
		goto done;
	}

	// Remove '\x01' prefix sentinels before linking
	runRemoveSentinelsPass(*mainModule);

done:
	if (mainModule == NULL) {
		std::string	s(diag.getMessage());
		klee_error(
			"error loading program '%s': %s",
			ifile.c_str(), s.c_str());
	}

	assert(mainModule && "unable to materialize");
}

ModuleOptions getMainModule(Module* &mainModule)
{
	loadInputBitcode(g_InputFile, mainModule);

	if (g_WithPOSIXRuntime) InitEnv = true;

	if (InitEnv) {
		int r = initEnv(mainModule);
		if (r != 0) {
			std::cerr << "Failed to init_env\n";
			exit(r);
		}
	}

	ModuleOptions Opts(false, false, ExcludeCovFiles);

	/* XXX: the posix and intrinsic stuff *should* be loaded
	 * after the posix code in order to link, but more test cases
	 * are failing because base klee is shit and grad school
	 * is fucked up. */
	mainModule = setupLibc(mainModule, Opts);
	loadIntrinsics(Opts);
	loadPOSIX(mainModule, Opts);

	return Opts;
}

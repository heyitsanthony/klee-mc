//===-- KModule.cpp -------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Internal/Module/KModule.h"
#include "../lib/Core/Context.h"
#include "../lib/Core/Executor.h"

#include "Passes.h"

#include "klee/Common.h"
#include "klee/Interpreter.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Support/ModuleUtil.h"

#include "llvm/Linker.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/ValueSymbolTable.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#if (LLVM_VERSION_MAJOR == 2 && LLVM_VERSION_MINOR == 6)
#include "llvm/Support/raw_os_ostream.h"
#endif
#include "llvm/System/Path.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/ModuleProvider.h"

#include "static/Sugar.h"

#include <sstream>

using namespace klee;
using namespace llvm;

namespace llvm {
	Pass *createLowerAtomicPass();
}

namespace {
  enum SwitchImplType {
    eSwitchTypeSimple,
    eSwitchTypeLLVM,
    eSwitchTypeInternal
  };

  cl::list<std::string>
  MergeAtExit("merge-at-exit");

  cl::opt<bool>
  NoTruncateSourceLines("no-truncate-source-lines",
                        cl::desc("Don't truncate long lines in the output source"));

  cl::opt<bool>
  OutputSource("output-source",
               cl::desc("Write the assembly for the final transformed source"),
               cl::init(true));

  cl::opt<bool>
  OutputModule("output-module",
               cl::desc("Write the bitcode for the final transformed module"),
               cl::init(false));

  cl::opt<SwitchImplType>
  SwitchType("switch-type", cl::desc("Select the implementation of switch"),
             cl::values(clEnumValN(eSwitchTypeSimple, "simple",
                                   "lower to ordered branches"),
                        clEnumValN(eSwitchTypeLLVM, "llvm",
                                   "lower using LLVM"),
                        clEnumValN(eSwitchTypeInternal, "internal",
                                   "execute switch internally"),
                        clEnumValEnd),
             cl::init(eSwitchTypeInternal));

  cl::opt<bool>
  DebugPrintEscapingFunctions("debug-print-escaping-functions",
                              cl::desc("Print functions whose address is taken."));
}

KModule::KModule(Module *_module)
: module(_module)
, targetData(new TargetData(module))
, kleeMergeFn(0)
, infos(0)
, constantTable(0)
, fpm(new FunctionPassManager(new ExistingModuleProvider(_module)))
{
}

KModule::~KModule()
{
	delete infos;

	delete fpm;

	foreach (it, functions.begin(), functions.end()) {
		delete *it;
	}

	delete targetData;
	delete module;
}

/***/

namespace llvm {
extern void Optimize(Module*);
}

// what a hack
Function *getStubFunctionForCtorList(
	Module *m,
	GlobalVariable *gv,
	std::string name)
{
  assert(!gv->isDeclaration() && !gv->hasInternalLinkage() &&
         "do not support old LLVM style constructor/destructor lists");

  std::vector<const Type*> nullary;

  Function *fn = Function::Create(
    FunctionType::get(
      Type::getVoidTy(getGlobalContext()), nullary, false),
      GlobalVariable::InternalLinkage,
      name,
      m);
  BasicBlock *bb = BasicBlock::Create(getGlobalContext(), "entry", fn);

  // From lli:
  // Should be an array of '{ int, void ()* }' structs.  The first value is
  // the init priority, which we ignore.
  ConstantArray *arr = dyn_cast<ConstantArray>(gv->getInitializer());
  if (!arr) goto gen_return;

  for (unsigned i=0; i<arr->getNumOperands(); i++) {
    ConstantStruct *cs = cast<ConstantStruct>(arr->getOperand(i));
    assert(cs->getNumOperands()==2 && "unexpected element in ctor initializer list");

    Constant *fp = cs->getOperand(1);
    if (fp->isNullValue()) continue;

    if (llvm::ConstantExpr *ce = dyn_cast<llvm::ConstantExpr>(fp))
      fp = ce->getOperand(0);

    if (Function *f = dyn_cast<Function>(fp)) {
      CallInst::Create(f, "", bb);
    } else {
      assert(0 && "unable to get function pointer from ctor initializer list");
    }
  }


gen_return:
  ReturnInst::Create(getGlobalContext(), bb);

  return fn;
}

static void injectStaticConstructorsAndDestructors(Module *m)
{
	GlobalVariable *ctors = m->getNamedGlobal("llvm.global_ctors");
	GlobalVariable *dtors = m->getNamedGlobal("llvm.global_dtors");

	if (!ctors && !dtors) return;

	Function *mainFn = m->getFunction("main");
	assert(mainFn && "unable to find main function");

	if (ctors)
		CallInst::Create(
			getStubFunctionForCtorList(m, ctors, "klee.ctor_stub"),
			"",
			mainFn->begin()->begin());
	if (dtors) {
		Function *dtorStub;
		dtorStub = getStubFunctionForCtorList(m, dtors, "klee.dtor_stub");
		foreach (it, mainFn->begin(), mainFn->end()) {
			if (!isa<ReturnInst>(it->getTerminator())) continue;
			CallInst::Create(dtorStub, "", it->getTerminator());
		}
	}
}

static void forceImport(Module *m, const char *name, const Type *retType, ...)
{
	// If module lacks an externally visible symbol for the name then we
	// need to create one. We have to look in the symbol table because
	// we want to check everything (global variables, functions, and
	// aliases).
	//
	// Note that the function will stay a declaration without
	// linking in the library after calling forceImport.

	Value *v = m->getValueSymbolTable().lookup(name);
	GlobalValue *gv = dyn_cast_or_null<GlobalValue>(v);

	if (gv && !gv->hasInternalLinkage()) return;
	va_list ap;

	va_start(ap, retType);
	std::vector<const Type *> argTypes;
	while (const Type *t = va_arg(ap, const Type*))
		argTypes.push_back(t);
	va_end(ap);

	m->getOrInsertFunction(
		name, FunctionType::get(retType, argTypes, false));
}

void KModule::prepareMerge(
	const Interpreter::ModuleOptions &opts,
	InterpreterHandler *ih)
{
	Function *mergeFn;

	mergeFn = module->getFunction("klee_merge");
	if (!mergeFn) {
		const llvm::FunctionType *Ty ;
		Ty =  FunctionType::get(
			Type::getVoidTy(getGlobalContext()),
			std::vector<const Type*>(), false);
		mergeFn = Function::Create(
			Ty,
			GlobalVariable::ExternalLinkage,
			"klee_merge",
			module);
	}


	foreach (it, MergeAtExit.begin(), MergeAtExit.end()) {
		addMergeExit(mergeFn, *it);
	}
}

void KModule::addMergeExit(Function* mergeFn, const std::string& name)
{
	Function *f;

	f = module->getFunction(name);
	if (!f) {
		klee_error("can't insert merge-at-exit for: %s (can't find)",
		name.c_str());
	} else if (f->isDeclaration()) {
		klee_error("can't insert merge-at-exit for: %s (external)",
		name.c_str());
	}

	BasicBlock	*exit;
	PHINode		*result;

	exit = BasicBlock::Create(getGlobalContext(), "exit", f);
	result = NULL;
	if (f->getReturnType() != Type::getVoidTy(getGlobalContext()))
		result = PHINode::Create(f->getReturnType(), "retval", exit);

	CallInst::Create(mergeFn, "", exit);
	ReturnInst::Create(getGlobalContext(), result, exit);

	llvm::errs() << "KLEE: adding klee_merge at exit of: " << name << "\n";
	foreach (bbit, f->begin(), f->end()) {
		Instruction *i;

		if (&*bbit == exit)
			continue;

		i = bbit->getTerminator();

		if (i->getOpcode() != Instruction::Ret)
			continue;

		if (result) {
			result->addIncoming(i->getOperand(0), bbit);
		}
		i->eraseFromParent();
		BranchInst::Create(exit, bbit);
	}
}

void KModule::outputSource(InterpreterHandler* ih)
{
    std::ostream *os = ih->openOutputFile("assembly.ll");
    assert(os && os->good() && "unable to open source output");

#if (LLVM_VERSION_MAJOR == 2 && LLVM_VERSION_MINOR == 6)
    // We have an option for this in case the user wants a .ll they
    // can compile.
    if (NoTruncateSourceLines) {
      os << *module;
    } else {
      bool truncated = false;
      std::string string;
      llvm::raw_string_ostream rss(string);
      rss << *module;
      rss.flush();
      const char *position = string.c_str();

      for (;;) {
        const char *end = index(position, '\n');
        if (!end) {
          os << position;
          break;
        } else {
          unsigned count = (end - position) + 1;
          if (count<255) {
            os->write(position, count);
          } else {
            os->write(position, 254);
            os << "\n";
            truncated = true;
          }
          position = end+1;
        }
      }
    }
#else
    llvm::raw_os_ostream *ros = new llvm::raw_os_ostream(*os);

    // We have an option for this in case the user wants a .ll they
    // can compile.
    if (NoTruncateSourceLines) {
      *ros << *module;
    } else {
      bool truncated = false;
      std::string string;
      llvm::raw_string_ostream rss(string);
      rss << *module;
      rss.flush();
      const char *position = string.c_str();

      for (;;) {
        const char *end = index(position, '\n');
        if (!end) {
          *ros << position;
          break;
        } else {
          unsigned count = (end - position) + 1;
          if (count<255) {
            ros->write(position, count);
          } else {
            ros->write(position, 254);
            *ros << "\n";
            truncated = true;
          }
          position = end+1;
        }
      }
    }
    delete ros;
#endif

    delete os;
}

void KModule::addModule(Module* in_mod)
{
	std::string	err;
	bool		isLinked;

	isLinked = Linker::LinkModules(module, in_mod, &err);
	foreach (it, in_mod->begin(), in_mod->end()) {
		Function	*kmod_f;
		KFunction	*kf;
		kmod_f = module->getFunction(it->getNameStr());
		assert (kmod_f != NULL);
		kf = addFunction(kmod_f);
		/* do not track coverage of modules (maybe do it in the future
		 * when I figure out a nice way to manage stats) */
		if (kf) kf->trackCoverage = false;
	}

//	assert (isLinked);
}

// Inject checks prior to optimization... we also perform the
// invariant transformations that we will end up doing later so that
// optimize is seeing what is as close as possible to the final
// module.

void KModule::injectRawChecks(const Interpreter::ModuleOptions &opts)
{
	PassManager pm;
	pm.add(new RaiseAsmPass(module));
	if (opts.CheckDivZero) pm.add(new DivCheckPass());

	pm.add(createLowerAtomicPass());
	// There was a bug in IntrinsicLowering which caches values which
	// may eventually  deleted (via RAUW).
	// This should now be fixed in LLVM
	pm.add(new IntrinsicCleanerPass(*targetData, false));
	pm.run(*module);

}

// Finally, run the passes that maintain invariants we expect during
// interpretation. We run the intrinsic cleaner just in case we
// linked in something with intrinsics but any external calls are
// going to be unresolved. We really need to handle the intrinsics
// directly I think?
void KModule::passEnforceInvariants(void)
{
	PassManager pm;

	pm.add(createCFGSimplificationPass());
	switch(SwitchType) {
	case eSwitchTypeInternal: break;
	case eSwitchTypeSimple: pm.add(new LowerSwitchPass()); break;
	case eSwitchTypeLLVM:  pm.add(createLowerSwitchPass()); break;
	default: klee_error("invalid --switch-type");
	}

	pm.add(createLowerAtomicPass());
	pm.add(new IntrinsicCleanerPass(*targetData));
	pm.add(new PhiCleanerPass());
	pm.run(*module);
}

void KModule::prepare(
	const Interpreter::ModuleOptions &opts,
	InterpreterHandler *ih)
{
  if (!MergeAtExit.empty())
  	prepareMerge(opts, ih);

  loadIntrinsicsLib(opts);
  injectRawChecks(opts);

  if (opts.Optimize) Optimize(module);

  // Needs to happen after linking (since ctors/dtors can be modified)
  // and optimization (since global optimization can rewrite lists).
  injectStaticConstructorsAndDestructors(module);

  passEnforceInvariants();

  // For cleanliness see if we can discard any of the functions we
  // forced to import.
  Function *f;
  f = module->getFunction("memcpy");
  if (f && f->use_empty()) f->eraseFromParent();
  f = module->getFunction("memmove");
  if (f && f->use_empty()) f->eraseFromParent();
  f = module->getFunction("memset");
  if (f && f->use_empty()) f->eraseFromParent();


  // Write out the .ll assembly file. We truncate long lines to work
  // around a kcachegrind parsing bug (it puts them on new lines), so
  // that source browsing works.
  if (OutputSource) outputSource(ih);

  if (OutputModule) {
    std::ostream *f = ih->openOutputFile("final.bc");
    llvm::raw_os_ostream* rfs = new llvm::raw_os_ostream(*f);
    WriteBitcodeToFile(module, *rfs);
    delete rfs;
    delete f;
  }

  kleeMergeFn = module->getFunction("klee_merge");

  /* Build shadow structures */

  infos = new InstructionInfoTable(module);

  foreach (it, module->begin(), module->end()) {
    addFunctionProcessed(it);
  }

  if (DebugPrintEscapingFunctions && !escapingFunctions.empty()) {
    llvm::errs() << "KLEE: escaping functions: [";
    foreach (it, escapingFunctions.begin(), escapingFunctions.end()) {
      llvm::errs() << (*it)->getName() << ", ";
    }
    llvm::errs() << "]\n";
  }

  
  fpm->add(new RaiseAsmPass(module));
  if (opts.CheckDivZero) fpm->add(new DivCheckPass());
  fpm->add(createLowerAtomicPass());
  fpm->add(new IntrinsicCleanerPass(*targetData));
  fpm->add(new PhiCleanerPass());
}

/* add a function that hasn't been cleaned up by any of our passes */
KFunction* KModule::addFunction(Function* f)
{
	if (f->isDeclaration()) return NULL;

	fpm->run(*f);
	fpm->doFinalization();
	fpm->doInitialization();

	return addFunctionProcessed(f);
}

KFunction* KModule::addFunctionProcessed(Function* f)
{
	KFunction	*kf;

	if (f->isDeclaration()) return NULL;

	kf = new KFunction(f, this);
	for (unsigned i=0; i<kf->numInstructions; ++i) {
		KInstruction *ki = kf->instructions[i];
		ki->info = &infos->getInfo(ki->inst);
	}

	functions.push_back(kf);
	functionMap.insert(std::make_pair(f, kf));
	/* Compute various interesting properties */
	if (functionEscapes(kf->function))
		escapingFunctions.insert(kf->function);

	return kf;
}

KConstant* KModule::getKConstant(Constant *c)
{
	std::map<llvm::Constant*, KConstant*>::iterator it = constantMap.find(c);
	if (it == constantMap.end()) return NULL;
	return NULL;
	return it->second;
}

unsigned KModule::getConstantID(Constant *c, KInstruction* ki)
{
	KConstant *kc = getKConstant(c);

	if (kc) return kc->id;

	unsigned id = constants.size();
	kc = new KConstant(c, id, ki);
	constantMap.insert(std::make_pair(c, kc));
	constants.push_back(c);
	return id;
}

/***/

KConstant::KConstant(llvm::Constant* _ct, unsigned _id, KInstruction* _ki)
{
  ct = _ct;
  id = _id;
  ki = _ki;
}

/***/
KFunction* KModule::getKFunction(llvm::Function* f) const
{
	std::map<llvm::Function*, KFunction*>::const_iterator it;

	it = functionMap.find(f);
	if (it == functionMap.end()) return NULL;

	return it->second;
}

KFunction* KModule::getKFunction(const char* fname) const
{
	Function	*f;

	f = module->getFunction(fname);
	if (f == NULL) return NULL;

	return getKFunction(f);
}

#include <iostream>

void KModule::loadIntrinsicsLib(const Interpreter::ModuleOptions &opts)
{
	// Force importing functions required by intrinsic lowering. Kind of
	// unfortunate clutter when we don't need them but we won't know
	// that until after all linking and intrinsic lowering is
	// done. After linking and passes we just try to manually trim these
	// by name. We only add them if such a function doesn't exist to
	// avoid creating stale uses.

	const llvm::Type *i8Ty = Type::getInt8Ty(getGlobalContext());
	const llvm::Type *i32Ty = Type::getInt32Ty(getGlobalContext());

	forceImport(
		module, "memcpy", PointerType::getUnqual(i8Ty),
		PointerType::getUnqual(i8Ty),
		PointerType::getUnqual(i8Ty),
		targetData->getIntPtrType(getGlobalContext()), (Type*) 0);
	forceImport(
		module, "memmove", PointerType::getUnqual(i8Ty),
		PointerType::getUnqual(i8Ty),
		PointerType::getUnqual(i8Ty),
		targetData->getIntPtrType(getGlobalContext()), (Type*) 0);
	forceImport(
		module, "memset", PointerType::getUnqual(i8Ty),
		PointerType::getUnqual(i8Ty),
		Type::getInt32Ty(getGlobalContext()),
		targetData->getIntPtrType(getGlobalContext()), (Type*) 0);

	forceImport(
		module, "atomic.load.add", Type::getInt32Ty(getGlobalContext()),
		PointerType::getUnqual(i32Ty),
		Type::getInt32Ty(getGlobalContext()),
		(Type*) 0);


	if (opts.CheckDivZero) {
		forceImport(module, "klee_div_zero_check",
			Type::getVoidTy(getGlobalContext()),
			Type::getInt64Ty(getGlobalContext()),
			NULL);
	}

	// FIXME: Missing force import for various math functions.

	// FIXME: Find a way that we can test programs without requiring
	// this to be linked in, it makes low level debugging much more
	// annoying.
	llvm::sys::Path path(opts.LibraryDir);

	path.appendComponent("libkleeRuntimeIntrinsic.bca");
	module = linkWithLibrary(module, path.c_str());
}

void KModule::bindModuleConstTable(Executor* exe)
{
	unsigned int	old_ct_sz;

	old_ct_sz = constantTable.size();
	for (unsigned i = old_ct_sz; i < constants.size(); i++) {
		Cell	c;
		c.value = exe->evalConstant(constants[i]);
		constantTable.push_back(c);
	}
}

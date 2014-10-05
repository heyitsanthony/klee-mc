//===-- ModuleUtil.cpp ----------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Passes.h"

#include "klee/Internal/Support/ModuleUtil.h"
#include "klee/Common.h"

#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/PassManager.h>
//#include <llvm/Assembly/AssemblyAnnotationWriter.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/CallSite.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/Support/Path.h>
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/Support/MemoryBuffer.h>
//#include <llvm/Support/system_error.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/SourceMgr.h>

#include "static/Sugar.h"

#include <map>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace llvm;
using namespace klee;



Function *klee::getDirectCallTarget(CallSite cs)
{
  llvm::ConstantExpr *ce;
  Value *v;


  v = cs.getCalledValue();
  if (Function *f = dyn_cast<Function>(v)) {
    return f;
  }

  ce = dyn_cast<llvm::ConstantExpr>(v);

  if (ce) {
    if (ce->getOpcode()==Instruction::BitCast)
      if (Function *f = dyn_cast<Function>(ce->getOperand(0)))
        return f;

    // NOTE: This assert may fire, it isn't necessarily a problem and
    // can be disabled, I just wanted to know when and if it happened.
    assert(0 && "FIXME: Unresolved direct target for a constant expression.");
  }

  return 0;
}

static bool valueIsOnlyCalled(const Value *v)
{
  foreach(it, v->use_begin(), v->use_end()) {
    if (const Instruction *instr = dyn_cast<Instruction>(*it)) {
      if (instr->getOpcode()==0) continue; // XXX function numbering inst
      if (!isa<CallInst>(instr) && !isa<InvokeInst>(instr)) return false;

      // Make sure that the value is only the target of this call and
      // not an argument.
      for (unsigned i=1,e=instr->getNumOperands(); i!=e; ++i)
        if (instr->getOperand(i)==v)
          return false;
    } else if (const llvm::ConstantExpr *ce =
               dyn_cast<llvm::ConstantExpr>(*it)) {
      if (ce->getOpcode()==Instruction::BitCast)
        if (valueIsOnlyCalled(ce))
          continue;
      return false;
    } else if (const GlobalAlias *ga = dyn_cast<GlobalAlias>(*it)) {
      // XXX what about v is bitcast of aliasee?
      if (v==ga->getAliasee() && !valueIsOnlyCalled(ga))
        return false;
    } else {
      return false;
    }
  }

  return true;
}

bool klee::functionEscapes(const Function *f) { return !valueIsOnlyCalled(f); }

void klee::runRemoveSentinelsPass(Module &module)
{
	llvm::PassManager pm;
	pm.add(new RemoveSentinelsPass());
	pm.run(module);
}

namespace klee {
Module* getBitcodeModule(const char* path);
Module* getBitcodeModule(const char* path)
{
	Module			*ret_mod;
	SMDiagnostic		diag;
	auto			Buffer(MemoryBuffer::getFile(path));

	if (!Buffer) {
		std::cerr <<  "Bad membuffer on " << path << std::endl;
		assert (Buffer && "Couldn't get mem buffer");
	}

	ret_mod = llvm::ParseIR(Buffer.get().get(), diag, getGlobalContext());
	if (ret_mod == NULL) {
		std::string	s(diag.getMessage());
		std::cerr
			<< "Error Parsing Bitcode File '"
			<< path << "': " << s << '\n';
	}
	
	assert (ret_mod && "Couldn't parse bitcode mod");

	auto err = ret_mod->materializeAllPermanently();
	if (err) {
		std::cerr << "Materialize failed..." << std::endl;
		assert (0 == 1 && "BAD MOD");
	}

	if (ret_mod == NULL) {
		std::cerr << "OOPS: (path=" << path << ")\n";
	}

	return ret_mod;
}
}

/// Loads library archive and links to it. Also runs RemoveSentinels pass
/// to remove \1 prefix characters in library routine names. This must be done
/// here so that we don't have false misses between called routines and
/// available library routines (e.g., '\x01fstatat64' in the library isn't
/// detected by calls to 'fstatat64' in the program).
///
/// Unfortunately, KLEE's built-in routines for linking against bitcode archives
/// don't allow us to load the archive and run passes prior to linking. Thus,
/// we've duplicated some of the functionality from
/// llvm/lib/Linker/LinkArchives.cpp here.
Module *klee::linkWithLibrary(
	Module *mod,
	const std::string &libraryName)
#if 0
{
	Linker			linker("klee", module,  0 /* Linker::Verbose */);
	llvm::sys::Path		libraryPath(libraryName);
	Archive			*a;
	std::vector<Module*>	m;
	std::string		err;


	a = Archive::OpenAndLoad(libraryPath, getGlobalContext(), &err);
	if (a == NULL) std::cerr << "Error OpenAndLoad: " << err << '\n';
	assert (a != NULL);

	if (a->getAllModules(m, &err)) {
		std::cerr << "Could not get all modules from "
			<< libraryName << '\n';
		assert (0 == 1);
	}
	
	foreach (it, m.begin(), m.end()) {
		if (linker.LinkInModule(*it, &err)) {
			std::cerr << "ERROR! " << err << '\n';
			assert(0 == 1);
		}
	}

	return linker.releaseModule();
}
#elif 0
{
	Linker			linker(module);
	llvm::sys::Path		libraryPath(libraryName);
	bool			err, native = false;

	// link against an unmodified version of the library archive for
	// the common case when no sentinels are found.
	// This avoids code duplication from llvm/lib/Linker/LinkArchives.cpp.
	err = linker.LinkInFile(libraryPath, native);
	if (err) {
		std::cerr << "Bad Link" << libraryName << '\n';
		assert (0 == 1);
	}

	return linker.releaseModule();
}
#else
{
	std::string		err_str;
	llvm::SMDiagnostic	err;
	llvm::Module		*new_mod;
	
	new_mod = llvm::ParseIRFile(libraryName, err, mod->getContext());
	if (new_mod == NULL) {
		std::cerr << "[Mod] Error parsing '" << libraryName << "'\n";
		std::cerr << "[Mod] Parse error: " << err.getMessage().str() << '\n';
		std::cerr << "[Mod] Parse error: " << err.getLineContents().str() << '\n';
		return NULL;
	}

	if (llvm::Linker::LinkModules(
		mod,
		new_mod,
		llvm::Linker::DestroySource,
		&err_str))
	{
		std::cerr
			<< "[Mod] Error loading '"
			<< libraryName << "' : " << err_str << '\n';
		return NULL;
	}

	return mod;
}
#endif

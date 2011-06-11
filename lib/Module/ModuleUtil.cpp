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

#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Linker.h"
#include "llvm/Module.h"
#include "llvm/ModuleProvider.h"
#include "llvm/PassManager.h"
#include "llvm/Assembly/AsmAnnotationWriter.h"
#include "llvm/Bitcode/Archive.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/System/Path.h"

#include <map>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace llvm;
using namespace klee;

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
Module *klee::linkWithLibrary(Module *module, 
                              const std::string &libraryName) {

  std::string errMsg;
  llvm::sys::Path libraryPath(libraryName);

  // Load symbol table for library archive
  Archive *libArchive = Archive::OpenAndLoad(libraryPath,
                                                    module->getContext(),
                                                    &errMsg);
  if (!libArchive)
    klee_error("failed to load library archive %s: %s",
               libraryPath.toString().c_str(), errMsg.c_str());


  const llvm::Archive::SymTabType& symbols = libArchive->getSymbolTable();

  std::set<ModuleProvider*> modulesToLoad;
  // Walk the symbol table and assemble a list of modules that need to loaded,
  // processed, and linked
  for (llvm::Archive::SymTabType::const_iterator si = symbols.begin();
       si != symbols.end(); ++si) {
    const std::string &symName = si->first;
    // check for \1 sentinel
    if(symName[0] == 1) {
      ModuleProvider *mp = libArchive->findModuleDefiningSymbol(symName,
                                                               &errMsg);
      if (!mp)
        klee_error("failed to find symbol %s: %s", symName.c_str(),
                   errMsg.c_str());
      modulesToLoad.insert(mp);
    }
  }

  Linker linker("klee", module, false);

  // Run passes on relevant modules and then link against them
  for (std::set<ModuleProvider*>::iterator mi = modulesToLoad.begin();
       mi != modulesToLoad.end(); ++mi) {
    Module *m = (*mi)->releaseModule(&errMsg);
    if (!m)
      klee_error("failed to release module: %s", errMsg.c_str());
    runRemoveSentinelsPass(*m);
    if (linker.LinkInModule(m, &errMsg)) {
      klee_error("failed to link with module %s->%s: %s",
                 libraryName.c_str(),
                 m->getModuleIdentifier().c_str(),
                 errMsg.c_str());
    }
    delete m;
  }

  delete libArchive;

  // Now link against an unmodified version of the library archive for
  // the common case when no sentinels are found. This avoids code duplication
  // from llvm/lib/Linker/LinkArchives.cpp.
  bool native = false;    
  if (linker.LinkInFile(libraryPath, native)) {
    assert(0 && "linking in library failed!");
  }

  return linker.releaseModule();
}

Function *klee::getDirectCallTarget(CallSite cs) {
  Value *v = cs.getCalledValue();

  if (Function *f = dyn_cast<Function>(v)) {
    return f;
  } else if (llvm::ConstantExpr *ce = dyn_cast<llvm::ConstantExpr>(v)) {
    if (ce->getOpcode()==Instruction::BitCast)
      if (Function *f = dyn_cast<Function>(ce->getOperand(0)))
        return f;

    // NOTE: This assert may fire, it isn't necessarily a problem and
    // can be disabled, I just wanted to know when and if it happened.
    assert(0 && "FIXME: Unresolved direct target for a constant expression.");
  }
  
  return 0;
}

static bool valueIsOnlyCalled(const Value *v) {
  for (Value::use_const_iterator it = v->use_begin(), ie = v->use_end();
       it != ie; ++it) {
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

bool klee::functionEscapes(const Function *f) {
  return !valueIsOnlyCalled(f);
}

void klee::runRemoveSentinelsPass(Module &module) {
  llvm::PassManager pm;
  pm.add(new RemoveSentinelsPass());
  pm.run(module);
}

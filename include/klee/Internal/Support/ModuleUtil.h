//===-- ModuleUtil.h --------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_TRANSFORM_UTIL_H
#define KLEE_TRANSFORM_UTIL_H

#include <memory>
#include <string>

namespace llvm {
  class Function;
  class Instruction;
  class Module;
  class CallSite;
}

namespace klee {
 
  /// Link a module with a specified bitcode archive.
  void linkWithLibrary(llvm::Module& module, 
                       const std::string &libraryName);

  /// Return the Function* target of a Call or Invoke instruction, or
  /// null if it cannot be determined (should be only for indirect
  /// calls, although complicated constant expressions might be
  /// another possibility).
  llvm::Function *getDirectCallTarget(llvm::CallSite);

  /// Return true iff the given Function value is used in something
  /// other than a direct call (or a constant expression that
  /// terminates in a direct call).
  bool functionEscapes(const llvm::Function *f);

  /// Remove '\x01' prefix sentinels from all function names in module
  void runRemoveSentinelsPass(llvm::Module &module);

  std::unique_ptr<llvm::Module> getBitcodeModule(const char* path);
}

#endif

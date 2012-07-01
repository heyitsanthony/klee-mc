//===-- RemoveSentinels.cpp -----------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Passes.h"

#include <iostream>

using namespace llvm;
using namespace klee;

char RemoveSentinelsPass::ID = 0;

bool RemoveSentinelsPass::runOnFunction(Function &F) {
  std::string funcName = F.getName().str();

  // Strip '\x01' prefix from function name
  if(funcName[0] == 1) {
    F.setName(funcName.substr(1));
    return true;
  }
  return false;
}

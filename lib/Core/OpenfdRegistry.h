//===-- OpenfdRegistry.h ----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_OPENFDREGISTRY_H
#define KLEE_OPENFDREGISTRY_H

#include <set>
#include <map>

namespace klee {

  class ExecutionState;

  namespace OpenfdRegistry
  {
    void fdOpened(ExecutionState*, int fd);
    void stateDestroyed(ExecutionState*);
  }

}

#endif

//===-- Searcher.h ----------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_SEARCHER_H
#define KLEE_SEARCHER_H

#include <vector>
#include <set>
#include <map>
#include <list>
// FIXME: Move out of header, use llvm streams.
#include <ostream>
#include <stdint.h>

namespace llvm {
  class BasicBlock;
  class Function;
  class Instruction;
}

namespace klee
{
  class ExecutionState;
  class Executor;

  class Searcher
  {
  public:
    virtual ~Searcher() {}
    virtual ExecutionState &selectState(bool allowCompact) = 0;

    virtual void update(ExecutionState *current,
                        const std::set<ExecutionState*> &addedStates,
                        const std::set<ExecutionState*> &removedStates,
                        const std::set<ExecutionState*> &ignoreStates,
                        const std::set<ExecutionState*> &unignoreStates) = 0;

    virtual bool empty() const = 0;

    // prints name of searcher as a klee_message()
    // TODO: could probably make prettier or more flexible
    virtual void printName(std::ostream &os) const {
      os << "<unnamed searcher>\n";
    }

    // pgbovine - to be called when a searcher gets activated and
    // deactivated, say, by a higher-level searcher; most searchers
    // don't need this functionality, so don't have to override.
    virtual void activate() {};
    virtual void deactivate() {};
    // utility functions

    void addState(ExecutionState *es, ExecutionState *current = 0) {
      std::set<ExecutionState*> tmp;
      tmp.insert(es);
      update(current, tmp, std::set<ExecutionState*>(),std::set<ExecutionState*>(),std::set<ExecutionState*>());
    }

    void removeState(ExecutionState *es, ExecutionState *current = 0) {
      std::set<ExecutionState*> tmp;
      tmp.insert(es);
      update(current, std::set<ExecutionState*>(), tmp,std::set<ExecutionState*>(),std::set<ExecutionState*>());
    }
  };
}

#endif

//===-- OpenfdRegistry.cpp ------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "OpenfdRegistry.h"

#include <iostream>
#include <unistd.h>

using namespace klee;

namespace klee
{
namespace OpenfdRegistry
{

std::map<int, std::set<ExecutionState*> > fd_es;
std::map<ExecutionState*, std::set<int> > es_fd;

void fdOpened(ExecutionState* es, int fd)
{
  fd_es[fd].insert(es);
  es_fd[es].insert(fd);
}

void stateDestroyed(ExecutionState* es)
{
  for (std::set<int>::const_iterator i = es_fd[es].begin(), e = es_fd[es].end(); i != e; ++i) {
    int fd = *i;
    fd_es[fd].erase(es);
    if (fd_es[fd].empty()) {
      close(fd);
      fd_es.erase(fd);
    }
  }
  es_fd.erase(es);
}

} // namespace OpenfdRegistry
} // namespace klee

//===-- UserSearcher.h ------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_USERSEARCHER_H
#define KLEE_USERSEARCHER_H

namespace klee {
  class Executor;
  class Searcher;

  // XXX gross, should be on demand?
  bool userSearcherRequiresMD2U();

  bool userSearcherRequiresBranchSequences();

  /* MAKE PRIVATE in facade */
  Searcher* setupInterleavedSearcher(Executor& executor, Searcher* s);
  Searcher* setupBaseSearcher(Executor& executor);
  Searcher* setupMergeSearcher(Executor& executor, Searcher* searcher);

  Searcher *constructUserSearcher(Executor &executor);
}

#endif

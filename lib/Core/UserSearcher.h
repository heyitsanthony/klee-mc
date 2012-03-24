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
class Prioritizer;

class UserSearcher
{
public:
	// XXX gross, should be on demand?
	static bool userSearcherRequiresMD2U();
	static bool userSearcherRequiresBranchSequences();
	static Searcher *constructUserSearcher(Executor &executor);
	static void setPrioritizer(Prioritizer* p) { prFunc = p; }

private:
	static Prioritizer	*prFunc;

	static Searcher* setupInterleavedSearcher(
		Executor& executor, Searcher* s);
	static Searcher* setupConfigSearcher(Executor& executor);
	static Searcher* setupBaseSearcher(Executor& executor);
	static Searcher* setupMergeSearcher(
		Executor& executor, Searcher* searcher);
};
}

#endif

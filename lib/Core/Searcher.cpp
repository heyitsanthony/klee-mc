//===-- Searcher.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Searcher.h"
#include "Executor.h"

#include "static/Sugar.h"

using namespace klee;

const std::set<ExecutionState*> Searcher::States::emptySet;

Searcher::Searcher() {}
Searcher::~Searcher() {}

Searcher::States::States(const std::set<ExecutionState*>& a)
: addedStates(a), removedStates(emptySet)
{}

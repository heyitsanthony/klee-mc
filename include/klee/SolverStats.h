//===-- SolverStats.h -------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_SOLVERSTATS_H
#define KLEE_SOLVERSTATS_H

#include "klee/Statistic.h"

namespace klee {
namespace stats {

  extern Statistic cexCacheTime;
  extern Statistic cexCacheHits;
  extern Statistic cexCacheMisses;
  extern Statistic queries;
  extern Statistic queriesTopLevel;
  extern Statistic queriesInvalid;
  extern Statistic queriesValid;
  extern Statistic queriesFailed;
  extern Statistic queryCacheHits;
  extern Statistic queryCacheMisses;
  extern Statistic queryConstructTime;
  extern Statistic queryConstructs;
  extern Statistic queryCounterexamples;
  extern Statistic queryTime;

}
}

#endif

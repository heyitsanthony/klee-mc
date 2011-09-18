//===-- Time.cpp ----------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Internal/System/Time.h"
#include "klee/SolverStats.h"
#include <llvm/Support/Process.h>

#include <iostream>

using namespace llvm;
using namespace klee;

double util::getUserTime() {
  sys::TimeValue now(0,0),user(0,0),sys(0,0);
  sys::Process::GetTimeUsage(now,user,sys);
  return (user.seconds() + (double) user.nanoseconds() * 1e-9);
}

double util::getWallTime() {
  sys::TimeValue time = sys::TimeValue::now();
  return time.seconds() + (double) time.nanoseconds() * 1e-9;
}

// weight coefficient for smooth instructions/sec averaging
static const double estimationWeight = 0.875;

// approx. number of times to call gettimeofday() per second; power of 2
// probably more efficient below (shift instead of div)
static const unsigned kUpdatesPerSec = 1024;


// efficient estimation of current wall time
double util::estWallTime() {
  static unsigned estimatedCallsPerSec = 100000; // initial estimate
  static unsigned callsSinceUpdate = 0;
  static unsigned lastQueries = 0;
  static double lastTime = util::getWallTime();
  double now = 0.;

  callsSinceUpdate++;

  if (callsSinceUpdate >= estimatedCallsPerSec / kUpdatesPerSec
      || stats::queries != lastQueries) { // poll current time

    now = util::getWallTime();

    // don't let queries skew our instructions/second estimate
    if(stats::queries == lastQueries) {
      estimatedCallsPerSec = estimatedCallsPerSec * estimationWeight +
        (1 - estimationWeight) * (callsSinceUpdate / (now - lastTime));
    }

    lastQueries = stats::queries;
    lastTime = now;
    callsSinceUpdate = 0;
  }
  else // extrapolate current time
    now = lastTime +
      ((double) callsSinceUpdate / (double) estimatedCallsPerSec);
  return now;
}

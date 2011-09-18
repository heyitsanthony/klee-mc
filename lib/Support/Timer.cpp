//===-- Timer.cpp ---------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Internal/Support/Timer.h"
#include "klee/Internal/System/Time.h"

#include <llvm/Support/Process.h>
#include <algorithm>

using namespace klee;
using namespace llvm;

WallTimer::WallTimer() {
  startMicroseconds = util::estWallTime() * 1000000.;
}

uint64_t WallTimer::check() {
  uint64_t now = (util::estWallTime() * 1000000.);
  // just in case 'now' is actual and 'start' was extrapolated to be greater
  // than actual; minimal loss of timing precision
  return std::max((uint64_t) 0, now - startMicroseconds);
}

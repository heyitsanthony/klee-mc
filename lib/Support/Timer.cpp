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

WallTimer::WallTimer() { reset(); }

void WallTimer::reset(void) { startTime = util::estWallTime(); }

uint64_t WallTimer::check()
{
	double now = util::estWallTime();
	return (uint64_t)((now - startTime)*1e6);
}

double WallTimer::checkSecs()
{
	double now = util::estWallTime();
	return (now - startTime);
}

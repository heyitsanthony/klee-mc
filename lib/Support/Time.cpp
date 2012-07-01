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
#include "klee/Internal/Support/cycle.h"
#include <iostream>

using namespace llvm;
using namespace klee;

double util::getUserTime()
{
	sys::TimeValue now(0,0),user(0,0),sys(0,0);
	sys::Process::GetTimeUsage(now,user,sys);
	return (user.seconds() + (double) user.nanoseconds() * 1e-9);
}


#define TICK_INTERVAL	10000000
double util::getWallTime()
{
	static sys::TimeValue	time = sys::TimeValue::now();
	static ticks		last_tick = 0;
	ticks			cur_tick, tick_diff;

	cur_tick = getticks();
	tick_diff = cur_tick - last_tick;
	if (tick_diff > TICK_INTERVAL) {
		last_tick = cur_tick;
		time = sys::TimeValue::now();
	}
	return time.seconds() + (double) time.nanoseconds() * 1e-9;
}

// efficient estimation of current wall time
double util::estWallTime() { return getWallTime(); }

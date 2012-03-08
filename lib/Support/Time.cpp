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

double util::getUserTime()
{
	sys::TimeValue now(0,0),user(0,0),sys(0,0);
	sys::Process::GetTimeUsage(now,user,sys);
	return (user.seconds() + (double) user.nanoseconds() * 1e-9);
}


double util::getWallTime()
{
	sys::TimeValue time = sys::TimeValue::now();
	return time.seconds() + (double) time.nanoseconds() * 1e-9;
}

// efficient estimation of current wall time
double util::estWallTime() { return getWallTime(); }

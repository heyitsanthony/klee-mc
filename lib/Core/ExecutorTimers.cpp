//===-- ExecutorTimers.cpp ------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CoreStats.h"
#include "Executor.h"
#include "ExeStateManager.h"
#include "PTree.h"
#include "StatsTracker.h"

#include "klee/Common.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/System/Time.h"

#include "llvm/Function.h"
#include "llvm/Support/CommandLine.h"

#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <math.h>


using namespace llvm;
using namespace klee;

cl::opt<double>
MaxTime("max-time",
        cl::desc("Halt execution after the specified number of seconds (0=off)"),
        cl::init(0));

///

class HaltTimer : public Executor::Timer {
  Executor *executor;

public:
  HaltTimer(Executor *_executor) : executor(_executor) {}
  ~HaltTimer() {}

  void run() {
    std::cerr << "KLEE: HaltTimer invoked\n";
    executor->setHaltExecution(true);
  }
};

///

static const double kSecondsPerCheck = 0.25;

// XXX hack
extern "C" unsigned dumpStates, dumpPTree;
unsigned dumpStates = 0, dumpPTree = 0;

void Executor::initTimers() {
  if (MaxTime) {
    addTimer(new HaltTimer(this), MaxTime);
  }
}

///

Executor::Timer::Timer() {}

Executor::Timer::~Timer() {}

class Executor::TimerInfo {
public:
  Timer *timer;
  
  /// Approximate delay per timer firing.
  double rate;
  /// Wall time for next firing.
  double nextFireTime;
  
public:
  TimerInfo(Timer *_timer, double _rate) 
    : timer(_timer),
      rate(_rate),
      nextFireTime(util::estWallTime() + rate) {}
  ~TimerInfo() { delete timer; }
};

void Executor::addTimer(Timer *timer, double rate) {
  timers.push_back(new TimerInfo(timer, rate));
}

void Executor::processTimersDumpStates(void)
{
  std::ostream *os = interpreterHandler->openOutputFile("states.txt");
  
  if (!os) goto done;

  for (ExeStateSet::const_iterator it = stateManager->begin(), 
    ie = stateManager->end(); it != ie; ++it)
  {
    ExecutionState *es = *it;
    *os << "(" << es << ",";
    *os << "[";
    ExecutionState::stack_ty::iterator next = es->stack.begin();
    ++next;
    for (ExecutionState::stack_ty::iterator sfIt = es->stack.begin(),
           sf_ie = es->stack.end(); sfIt != sf_ie; ++sfIt) {
      *os << "('" << sfIt->kf->function->getNameStr() << "',";
      if (next == es->stack.end()) {
        *os << es->prevPC->info->line << "), ";
      } else {
        *os << next->caller->info->line << "), ";
        ++next;
      }
    }
    *os << "], ";

    StackFrame &sf = es->stack.back();
    uint64_t md2u = computeMinDistToUncovered(es->pc,
                sf.minDistToUncoveredOnReturn);
    uint64_t icnt = theStatisticManager->getIndexedValue(stats::instructions,
                     es->pc->info->id);
    uint64_t cpicnt = sf.callPathNode->statistics.getValue(stats::instructions);

    *os << "{";
    *os << "'depth' : " << es->depth << ", ";
    *os << "'weight' : " << es->weight << ", ";
    *os << "'queryCost' : " << es->queryCost << ", ";
    *os << "'coveredNew' : " << es->coveredNew << ", ";
    *os << "'instsSinceCovNew' : " << es->instsSinceCovNew << ", ";
    *os << "'md2u' : " << md2u << ", ";
    *os << "'icnt' : " << icnt << ", ";
    *os << "'CPicnt' : " << cpicnt << ", ";
    *os << "}";
    *os << ")\n";
  }

  delete os;

done:
  dumpStates = 0;
}

void Executor::processTimers(ExecutionState *current,
                             double maxInstTime)
{
  static double lastCall = 0., lastCheck = 0.;
  double now = util::estWallTime();

  if (dumpPTree) {
    char name[32];
    sprintf(name, "ptree%08d.dot", (int) stats::instructions);
    std::ostream *os = interpreterHandler->openOutputFile(name);
    if (os) {
      processTree->dump(*os);
      delete os;
    }
    
    dumpPTree = 0;
  }

  if (dumpStates) processTimersDumpStates();

  if (now - lastCheck <= kSecondsPerCheck) goto done;

  if (maxInstTime>0 && current && !stateManager->isRemovedState(current)
      && lastCall != 0. && (now - lastCall) > maxInstTime) {
    klee_warning("max-instruction-time exceeded: %.2fs",
                 now - lastCall);
    terminateStateEarly(*current, "max-instruction-time exceeded");
  }

  if (timers.empty()) goto done;

  for (std::vector<TimerInfo*>::iterator it = timers.begin(), 
         ie = timers.end(); it != ie; ++it) {
    TimerInfo *ti = *it;
  
    if (now >= ti->nextFireTime) {
      ti->timer->run();
      ti->nextFireTime = now + ti->rate;
    }
  }

done:
  lastCall = now;
}

void Executor::deleteTimerInfo(TimerInfo*& p)
{
  delete p;
  p = 0;
}

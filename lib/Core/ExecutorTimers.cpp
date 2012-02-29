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
#include "static/Sugar.h"

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

double MaxTime;

cl::opt<double, true>
MaxTimeProxy("max-time",
        cl::desc("Halt execution after the specified number of seconds (0=off)"),
	cl::location(MaxTime),
        cl::init(0));

cl::opt<unsigned>
DumpStateStats("dump-statestats",
        cl::desc("Dump state stats every n seconds (0=off)"),
        cl::init(0));


class HaltTimer : public Executor::Timer
{
public:
	HaltTimer(Executor *_executor) : executor(_executor) {}
	virtual ~HaltTimer() {}

	void run()
	{
		std::cerr << "KLEE: HaltTimer invoked\n";
		executor->setHaltExecution(true);
	}
private:
	Executor *executor;
};

#define USE_PREV_SIG	false
class SigUsrTimer : public Executor::Timer
{
public:
	SigUsrTimer(Executor *_executor)
	: executor(_executor)
	{
		prev_sigh = signal(SIGUSR2, sigusr_handler);
	}

	virtual ~SigUsrTimer() { signal(SIGUSR2, prev_sigh); }

	void run()
	{
		std::ostream	*os;

		if (!sigusr_triggered)
			return;

		sigusr_triggered = false;

		os = executor->getInterpreterHandler()->openOutputFile("tr.txt");
		if (os == NULL) {
			std::cerr << "[SIGUSR] Couldn't open tr.txt!\n";
			return;
		}

		foreach (it, executor->beginStates(), executor->endStates()) {
			executor->printStackTrace(*(*it), *os);
		}

		delete os;
	}

	static void sigusr_handler(int k)
	{	sigusr_triggered = true;
		if (	USE_PREV_SIG &&
			prev_sigh != SIG_DFL && prev_sigh != SIG_IGN)
		{
			prev_sigh(k);
		}
	}
private:
	static bool		sigusr_triggered;
	static sighandler_t	prev_sigh;
	Executor		*executor;
};
bool SigUsrTimer::sigusr_triggered = false;
sighandler_t SigUsrTimer::prev_sigh = NULL;


class StatTimer : public Executor::Timer
{
public:
	virtual ~StatTimer() { if (os) delete os;}

protected:
	StatTimer(Executor *_executor, const char* fname)
	: executor(_executor)
	, n(0)
	{ os = executor->getInterpreterHandler()->openOutputFile(fname); 
	  base_time = util::estWallTime();
	}

	void run()
	{
		if (!os) return;

		double cur_time = util::estWallTime();
		*os << (cur_time - base_time) << ' ';
		print();
		*os << '\n';
		os->flush();
	}

	virtual void print(void) = 0;

	double		base_time;
	Executor	*executor;
	std::ostream 	*os;
	unsigned	n;
};

#include <malloc.h>
#include "MemUsage.h"
#include "Memory.h"
cl::opt<unsigned>
DumpMemStats("dump-memstats",
        cl::desc("Dump memory stats every n seconds (0=off)"),
        cl::init(0));
class MemStatTimer : public StatTimer
{
public:
	MemStatTimer(Executor *_exe) : StatTimer(_exe, "mem.txt") {}
protected:
	void print(void) { *os << ObjectState::getNumObjStates() <<
		' ' << getMemUsageMB() <<
		' ' << mallinfo().uordblks <<
		' ' << HeapObject::getNumHeapObjs(); }
};

class StateStatTimer : public StatTimer
{
public:
	StateStatTimer(Executor *_exe) : StatTimer(_exe, "state.txt") {}
protected:
	void print(void) {
		*os <<
		executor->getNumStates() << ' ' <<
		executor->getNumFullStates(); }
};

#include "../Expr/ExprAlloc.h"
cl::opt<unsigned>
DumpExprStats("dump-exprstats",
        cl::desc("Dump expr stats every n seconds (0=off)"),
        cl::init(0));
class ExprStatTimer : public StatTimer
{
public:
	ExprStatTimer(Executor *_exe) : StatTimer(_exe, "expr.txt") {}
protected:
	void print(void) { *os
		<< Expr::getNumExprs() << ' '
		<< Array::getNumArrays() << ' '
		<< ExprAlloc::getNumConstants(); }
};

extern unsigned g_cachingsolver_sz;
extern unsigned g_cexcache_sz;

#include "../Solver/CachingSolver.h"
cl::opt<unsigned>
DumpCacheStats("dump-cachestats",
        cl::desc("Dump cache stats every n seconds (0=off)"),
        cl::init(0));
class CacheStatTimer : public StatTimer
{
public:
	CacheStatTimer(Executor *_exe) : StatTimer(_exe, "cache.txt") {}
protected:
	void print(void) { *os
		<< g_cachingsolver_sz << ' '
		<< g_cexcache_sz << ' '
		<< CachingSolver::getHits() << ' '
		<< CachingSolver::getMisses(); }
};


cl::opt<unsigned>
DumpCovStats("dump-covstats",
        cl::desc("Dump coverage stats every n seconds (0=off)"),
        cl::init(0));
class CovStatTimer : public StatTimer
{
public:
	CovStatTimer(Executor *_exe) : StatTimer(_exe, "cov.txt") {}
protected:
	void print(void) { *os
		<< stats::coveredInstructions << ' '
		<< stats::uncoveredInstructions << ' '
		<< stats::instructions; }
};

#include "../Expr/RuleBuilder.h"
cl::opt<unsigned>
DumpRuleBuilderStats("dump-rbstats",
        cl::desc("Dump rule builder stats every n seconds (0=off)"),
        cl::init(0));
class RuleBuilderStatTimer : public StatTimer
{
public:
	RuleBuilderStatTimer(Executor *_exe) : StatTimer(_exe, "rb.txt") {}
protected:
	void print(void) { *os
		<< RuleBuilder::getHits() << ' '
		<< RuleBuilder::getMisses() << ' '
		<< RuleBuilder::getRuleMisses() << ' '
		<< RuleBuilder::getNumRulesUsed(); }
};

///

static const double kSecondsPerCheck = 0.25;

// XXX hack
extern "C" unsigned dumpStates, dumpPTree;
unsigned dumpStates = 0, dumpPTree = 0;

void Executor::initTimers(void)
{
	if (MaxTime)
		addTimer(new HaltTimer(this), MaxTime);

	if (DumpRuleBuilderStats)
		addTimer(new RuleBuilderStatTimer(this), DumpRuleBuilderStats);

	if (DumpMemStats)
		addTimer(new MemStatTimer(this), DumpMemStats);

	if (DumpStateStats)
		addTimer(new StateStatTimer(this), DumpStateStats);

	if (DumpExprStats)
		addTimer(new ExprStatTimer(this), DumpExprStats);

	if (DumpCacheStats)
		addTimer(new CacheStatTimer(this), DumpCacheStats);

	if (DumpCovStats)
		addTimer(new CovStatTimer(this), DumpCovStats);

	addTimer(new SigUsrTimer(this), 1);
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

  foreach (it,  stateManager->begin(), stateManager->end()) {
    ExecutionState *es = *it;
    *os << "(" << es << ",";
    *os << "[";
    ExecutionState::stack_ty::iterator next = es->stack.begin();
    ++next;
    foreach (sfIt,  es->stack.begin(), es->stack.end()) {
      *os << "('" << sfIt->kf->function->getNameStr() << "',";
      if (next == es->stack.end()) {
        *os << es->prevPC->getInfo()->line << "), ";
      } else {
        *os << next->caller->getInfo()->line << "), ";
        ++next;
      }
    }
    *os << "], ";

    StackFrame &sf = es->stack.back();
    uint64_t md2u, icnt, cpicnt;

    md2u = computeMinDistToUncovered(es->pc, sf.minDistToUncoveredOnReturn);
    icnt = theStatisticManager->getIndexedValue(
    	stats::instructions, es->pc->getInfo()->id);
    cpicnt = sf.callPathNode->statistics.getValue(stats::instructions);
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
      pathTree->dump(*os);
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

  foreach (it, timers.begin(), timers.end()) {
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

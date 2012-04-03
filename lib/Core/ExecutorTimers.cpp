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

#include <fstream>

#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
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

#include "../Expr/ExprAlloc.h"
cl::opt<unsigned>
UseGCTimer("gc-timer",
        cl::desc("Periodically garbage collect expressions (default=60s)."),
        cl::init(60));
class ExprGCTimer : public Executor::Timer
{
public:
	ExprGCTimer(Executor *_executor) : executor(_executor) {}
	virtual ~ExprGCTimer() {}

	void run()
	{
		std::cerr << "KLEE: ExprGC invoked\n";
		ExprAlloc	*ea;
		ea = Expr::getAllocator();
		ea->garbageCollect();
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

class PyStatTimer : public StatTimer
{
public:
	virtual ~PyStatTimer() {}
protected:
	PyStatTimer(Executor *_exe, const char* fname)
	: StatTimer(_exe, fname) {}

	void run(void)
	{
		if (!os) return;
		double cur_time = util::estWallTime();
		*os << '[';
		*os << (cur_time - base_time) << ", ";
		print();
		*os << "]\n";
		os->flush();
	}
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

#include "klee/SolverStats.h"
cl::opt<unsigned>
DumpQueryStats("dump-querystats",
        cl::desc("Dump query stats every n seconds (0=off)"),
        cl::init(0));
class QueryStatTimer : public StatTimer
{
public:
	QueryStatTimer(Executor *_exe) : StatTimer(_exe, "query.txt") {}
protected:
	void print(void) { *os
		<< stats::queries << ' '
		<< stats::queryTime << ' '
		<< stats::solverTime; }
};

#include "klee/Internal/Module/KInstruction.h"
cl::opt<unsigned>
DumpBrData("dump-br-data",
	cl::desc("Dump branch data (0=off)"),
	cl::init(0));
class BrDataTimer : public Executor::Timer
{
public:
	BrDataTimer(Executor* _exe) : exe(_exe) {}
	virtual ~BrDataTimer() {}

	void run(void)
	{
		std::ostream* os;

		os = exe->getInterpreterHandler()->openOutputFile(
			"brdata.txt");
		if (os == NULL) return;

		foreach (it,
			KBrInstruction::beginBr(),
			KBrInstruction::endBr())
		{
			KBrInstruction	*kbr = *it;
			llvm::Function	*parent_f;

			parent_f = kbr->getInst()->getParent()->getParent();
			(*os)	<< exe->getPrettyName(parent_f)
				<< ' ' << kbr->getTrueHits()
				<< ' ' << kbr->getFalseHits()
				<< ' ' << kbr->getForkHits()
				<< ' ' << kbr->getTrueMinInst()
				<< ' ' << kbr->getFalseMinInst()
				<< '\n';
		}
		delete os;
	}
private:
	Executor* exe;
};

cl::opt<unsigned>
DumpStateInstStats("dump-stateinststats",
        cl::desc("Dump state inst stats every n seconds (0=off)"),
        cl::init(0));
class StateInstStatTimer : public PyStatTimer
{
public:
	StateInstStatTimer(Executor *_exe) : PyStatTimer(_exe, "sinst.txt") {}
protected:
	void print(void)
	{
		std::map<unsigned, unsigned>	inst_buckets;

		foreach (it, executor->beginStates(), executor->endStates()) {
			const ExecutionState*	es = *it;
			unsigned		old_c, b_idx;

			b_idx = es->totalInsts / 1000;
			old_c = inst_buckets[b_idx];
			inst_buckets[b_idx] = old_c+1;
		}

		/* (~ num insts dispatched, num states) */
		foreach (it, inst_buckets.begin(), inst_buckets.end()) {
			*os	<< "(" << (it->first*1000)
				<< ", " << it->second << "), ";
		}
	}
};


class UserCommand
{
public:
	UserCommand(Executor *_exe) : exe(_exe) {}
	virtual ~UserCommand() {}
	virtual void run(std::istream& is, std::ostream& os) = 0;
protected:
	Executor	*exe;
};

#define DECL_PIPECMD(x)			\
class PC##x : public UserCommand {	\
public:					\
	PC##x(Executor* _exe) : UserCommand(_exe) {}		\
	virtual ~PC##x () {}					\
	virtual void run(std::istream &is, std::ostream& os);	\
};					\
void PC##x::run(std::istream& is, std::ostream& os)	\

static KFunction* lookupPrettyModule(
	Executor* exe, const std::string& func_name)
{
	foreach (
		it,
		exe->getKModule()->kfuncsBegin(),
		exe->getKModule()->kfuncsEnd())
	{
		std::string name;
		name = exe->getPrettyName((*it)->function);
		if (name == func_name) {
			return *it;
		}
	}

	return NULL;
}

#include "static/DijkstrasShortestPath.h"
static void dumpStateDist(
	Executor* exe, ExecutionState* es,
	const std::string& dst_func,
	DijkstrasShortestPath<const KFunction*>& dsp,
	LabelGraph<const KFunction*, int64_t>&	back_graph)
{
	unsigned	depth = 0;

	foreach (it, es->stack.rbegin(), es->stack.rend()) {
		const KFunction	*cur_kf;
		int64_t		dist;

		cur_kf = (*it).kf;
		if (cur_kf == NULL)
			break;

		dist = dsp.dist(cur_kf);
		if (dist > 0xffffffff || dist < 0) {
			depth++;
			continue;
		}

		dist += depth*10;

		std::cerr << dist << " : "
			<< exe->getPrettyName(cur_kf->function)
			<< " -> " << dst_func << ": "
			<< ". StInst="
			<< es->totalInsts
			<< "[" << depth << "]"
			<< '\n';
		exe->printStackTrace(*es, std::cerr);
		break;
	}
}

DECL_PIPECMD(DumpDistance)
{
	KFunction	*kf;
	std::string	func_name;

	is >> func_name;
	if (func_name.size() == 0) {
		os << "[DumpDist] NO FUNCTION GIVEN!\n";
		return;
	}

	/* given a state, compute the shortest path to given func */
	/* XXX: need to use djikstra to find shortest paths */
	std::cerr << "[DumpDist] New dist " << func_name << '\n';
	
	kf = lookupPrettyModule(exe, func_name);
	if (!kf) {
		std::cerr << "[DumpDist] Not found: " << func_name << '\n';
		return;
	}


	kf->function->dump();

	LabelGraph<const KFunction*, int64_t>	exit_graph, back_graph;
	std::cerr << "[DD] Creating exit graph\n";
	foreach (
		it,
		exe->getKModule()->kfuncsBegin(),
		exe->getKModule()->kfuncsEnd())
	{
		const KFunction	*cur_kf = *it;

		foreach (it, cur_kf->beginExits(), cur_kf->endExits()) {
			exit_graph.addEdge(cur_kf, *it, 1);
			back_graph.addEdge(*it, cur_kf, 1);
		}
	}

	std::cerr << "[DD] Computing shortest path\n";

	DijkstrasShortestPath<const KFunction*>	dsp(&exit_graph, kf);

	foreach (it, exe->beginStates(), exe->endStates())
		dumpStateDist(exe, *it, func_name, dsp, back_graph);
}


class UserCommandTimer : public Executor::Timer
{
public:
	UserCommandTimer(Executor *_exe) : exe(_exe), fd(-1)
	{
		cmds["dumpstates"] = new PCDumpDistance(exe);
	}
	virtual ~UserCommandTimer()
	{
		foreach (it, cmds.begin(), cmds.end())
			delete it->second;
		close(fd);
	}
	void run(void);
private:
	typedef std::map<std::string, UserCommand*> cmds_ty;
	UserCommand* get(const char* buf)
	{
		cmds_ty::const_iterator it(cmds.find(buf));
		if (it == cmds.end())
			return NULL;
		return it->second;
	}

	Executor	*exe;
	int		fd;
	cmds_ty		cmds;
};


cl::opt<bool>
UseCmdUser("cmdpipe",
        cl::desc("Let user enter commands using cmdklee file."),
        cl::init(false));

void UserCommandTimer::run(void)
{
	UserCommand	*pc;
	std::ifstream	ifs("cmdklee");
	std::string	s;

	if (!ifs.good() || ifs.bad() || ifs.fail())
		return;

	if (!(ifs >> s))
		return;

	std::cerr << "[CmdUser] WOO: " << s << '\n';
	pc = get(s.c_str());
	if (pc != NULL)
		pc->run(ifs, std::cerr);

	ifs.close();
	unlink("cmdklee");
}

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

	if (DumpQueryStats)
		addTimer(new QueryStatTimer(this), DumpQueryStats);

	if (DumpBrData)
		addTimer(new BrDataTimer(this), DumpBrData);

	if (UseGCTimer)
		addTimer(new ExprGCTimer(this), UseGCTimer);

	if (DumpStateInstStats)
		addTimer(new StateInstStatTimer(this), DumpStateInstStats);

	if (UseCmdUser)
		addTimer(new UserCommandTimer(this), 2);

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

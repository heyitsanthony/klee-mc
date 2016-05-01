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
#include "StateSolver.h"
#include "PTree.h"
#include "StatsTracker.h"
#include "OOMTimer.h"
#include "static/Sugar.h"

#include "klee/Common.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/System/Time.h"
#include "klee/Statistics.h"

#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"

#include <fstream>
#include <sstream>

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

cl::opt<double>
MaxTimeNoProgress("max-time-no-progress",
        cl::desc("Halt after specified seconds after making no progress"), 
        cl::init(0));


class HaltTimer : public Executor::Timer
{
public:
	HaltTimer(Executor &_exe) : exe(_exe) {}

	void run() override {
		std::cerr << "KLEE: HaltTimer invoked\n";
		exe.setHaltExecution(true);
	}
private:
	Executor &exe;
};

class HaltNoProgressTimer : public Executor::Timer
{
public:
	HaltNoProgressTimer(Executor &_exe)
	: exe(_exe), numCovInstrs(0) {}

	void run() override {
		if (numCovInstrs < stats::coveredInstructions) {
			numCovInstrs = stats::coveredInstructions;
			return;
		}
		std::cerr << "KLEE: Halting-- no progress!\n";
		exe.setHaltExecution(true);
	}
private:
	Executor &exe;
	unsigned numCovInstrs;
};



#include "../Expr/ExprAlloc.h"
cl::opt<unsigned>
UseGCTimer("gc-timer",
        cl::desc("Periodically garbage collect expressions (default=60s)."),
        cl::init(60));
class ExprGCTimer : public Executor::Timer
{
public:
	ExprGCTimer(Executor &_exe) {}
	virtual ~ExprGCTimer() {}

	void run()
	{
		std::cerr << "KLEE: ExprGC invoked\n";
		ExprAlloc	*ea;
		ea = Expr::getAllocator();
		ea->garbageCollect();
		Array::garbageCollect();
		ObjectState::garbageCollect();
	}
private:
};

#include "ObjectState.h"
cl::opt<unsigned>
UseObjScanTimer("objscan-timer",
        cl::desc("Periodically garbage collect expressions (default=60s)."),
        cl::init(0));
class ExprObjScanTimer : public Executor::Timer
{
public:
	ExprObjScanTimer(Executor &_exe)  {}

	void run() override {
		std::cerr << "KLEE: ExprObjScan invoked\n";
	//	Array::garbageCollect();
	//	assert (0 ==1  && "STUB");
	}
private:
};

class StatTimer : public Executor::Timer
{
public:
protected:
	StatTimer(Executor &_exe, const char* fname)
	: exe(_exe)
	, n(0)
	{
		os = exe.getInterpreterHandler()->openOutputFile(fname);
		base_time = util::estWallTime();
	}

	void run() override {
		if (!os) return;

		double cur_time = util::estWallTime();
		*os << (cur_time - base_time) << ' ';
		print();
		*os << '\n';
		os->flush();
	}

	virtual void print(void) = 0;

	double		base_time;
	Executor	&exe;
	std::unique_ptr<std::ostream> os;
	unsigned	n;
};

class PyStatTimer : public StatTimer
{
public:
	virtual ~PyStatTimer() {}
protected:
	PyStatTimer(Executor &_exe, const char* fname)
	: StatTimer(_exe, fname) {}

	void run(void) override {
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
	MemStatTimer(Executor &_exe) : StatTimer(_exe, "mem.txt") {}
protected:
	void print(void) { *os << ObjectState::getNumObjStates() <<
		' ' << getMemUsageMB() <<
		' ' << UpdateList::getCount(); }
};

class StateStatTimer : public StatTimer
{
public:
	StateStatTimer(Executor &_exe) : StatTimer(_exe, "state.txt") {}
protected:
	void print(void) {
		*os <<
		exe.getNumStates() << ' ' <<
		exe.getNumFullStates() << ' ' <<
		exe.getHandler().getNumTestCases() << ' ' <<
		exe.getHandler().getNumPathsExplored(); }
};

#include "../Expr/ExprAlloc.h"
cl::opt<unsigned>
DumpExprStats("dump-exprstats",
        cl::desc("Dump expr stats every n seconds (0=off)"),
        cl::init(0));
class ExprStatTimer : public StatTimer
{
public:
	ExprStatTimer(Executor &_exe) : StatTimer(_exe, "expr.txt") {}
protected:
	void print(void) { *os
		<< Expr::getNumExprs() << ' '
		<< Array::getNumArrays() << ' '
		<< ExprAlloc::getNumConstants() << ' '
		<< Expr::getNumExprs() - ExprAlloc::getNumConstants(); }
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
	CacheStatTimer(Executor &_exe) : StatTimer(_exe, "cache.txt") {}
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
	CovStatTimer(Executor &_exe) : StatTimer(_exe, "cov.txt") {}
protected:
	void print(void) { *os
		<< stats::coveredInstructions << ' '
		<< stats::uncoveredInstructions << ' '
		<< stats::instructions; }
};

#include "../Expr/RuleBuilder.h"
#include "../Expr/ExprPatternMatch.h"
cl::opt<unsigned>
DumpRuleBuilderStats("dump-rbstats",
        cl::desc("Dump rule builder stats every n seconds (0=off)"),
        cl::init(0));
class RuleBuilderStatTimer : public StatTimer
{
public:
	RuleBuilderStatTimer(Executor &_exe) : StatTimer(_exe, "rb.txt") {}
protected:
	void print(void) { *os
		<< RuleBuilder::getHits() << ' '
		<< RuleBuilder::getMisses() << ' '
		<< ExprPatternMatch::getConstHit() << ' '
		<< ExprPatternMatch::getConstMiss() << ' '
		<< RuleBuilder::getRuleMisses() << ' '
		<< RuleBuilder::getNumRulesUsed() << ' '
		<< RuleBuilder::getFiltered() << ' '
		<< RuleBuilder::getFilterSize() << ' '; }
};

#include "Forks.h"
#include "klee/SolverStats.h"
#include "MMU.h"
#include "../Solver/IndependentSolver.h"
#include "../Solver/SolverImpl.h"
cl::opt<unsigned>
DumpQueryStats("dump-querystats",
        cl::desc("Dump query stats every n seconds (0=off)"),
        cl::init(0));
class QueryStatTimer : public StatTimer
{
public:
	QueryStatTimer(Executor &_exe) : StatTimer(_exe, "query.txt") {}
protected:
	void print(void)
	{
		printTimes();
		*os
		<< StateSolver::getRealQueries() << ' '
		<< stats::queries << ' '
		<< MMU::getQueries() << ' '
		<< IndependentSolver::getIndependentCount() << ' '
		<< stats::queryTime << ' '
		<< stats::solverTime << ' '
		<< StateSolver::getConstQueries() << ' '
		<< SolverImpl::getImpliedValids() << ' '
		<< Solver::getNumGetValue();
	}


	void printTimes(void) {
		auto f = exe.getInterpreterHandler()->openOutputFile("qtimes.txt");
		if (f == NULL) return;
		StateSolver::dumpTimes(*f);
	}
// stats::queriesTopLevel
};

#include "klee/Internal/Module/KInstruction.h"
cl::opt<unsigned>
DumpBrData("dump-br-data",
	cl::desc("Dump branch data (0=off)"),
	cl::init(0));
cl::opt<unsigned>
DumpBrExprData("dump-br-expr-data",
	cl::desc("Dump branch w/ expression data (0=off)"),
	cl::init(0));

class BrDataTimer : public Executor::Timer
{
public:
	BrDataTimer(
		const char* _fname,
		Executor &_exe,
		bool _ignoreConcrete=false)
	: fname(_fname)
	, exe(_exe)
	, ignoreConcrete(_ignoreConcrete) {}

	void run(void) override {
		auto os = exe.getInterpreterHandler()->openOutputFile(fname);
		if (os == NULL) return;

		foreach (it,
			KBrInstruction::beginBr(),
			KBrInstruction::endBr())
		{
			const KBrInstruction	*kbr = *it;
			const llvm::Function	*parent_f;

			/* only dump branches that have been executed */
			if (	kbr->getTrueHits() == 0 &&
				kbr->getFalseHits() == 0 &&
				kbr->getForkHits() == 0)
				continue;

			if (ignoreConcrete && !kbr->hasSeenExpr())
				continue;

			parent_f = kbr->getInst()->getParent()->getParent();
			(*os)	<< exe.getKModule()->getPrettyName(parent_f)
				<< ' ' << kbr->getTrueHits()
				<< ' ' << kbr->getFalseHits()
				<< ' ' << kbr->getForkHits()
				<< ' ' << kbr->getTrueMinInst()
				<< ' ' << kbr->getFalseMinInst()
				<< ' ' << kbr->getTrueFollows()
				<< ' ' << kbr->getFalseFollows()
				<< ' ' << kbr->getSeenExprs()
				<< '\n';
		}
	}
private:
	const char	*fname;
	Executor	&exe;
	bool		ignoreConcrete;
};

cl::opt<unsigned>
DumpCovData("dump-cov-data",
	cl::desc("Dump coverage data every n seconds (0=off)"),
	cl::init(0));

class CovTimer : public Executor::Timer
{
public:
	CovTimer(
		const char* _fname,
		Executor &_exe,
		bool _ignoreConcrete=false)
	: fname(_fname)
	, exe(_exe)
	, ignoreConcrete(_ignoreConcrete) {}

	void run(void) override {
		KModule*	kmod;

		auto os = exe.getInterpreterHandler()->openOutputFile(fname);
		if (os == NULL) return;

		assert(!ignoreConcrete && "STUB STUB STUB");

		kmod = exe.getKModule();
		foreach (it, kmod->kfuncsBegin(), kmod->kfuncsEnd()) {
			const KFunction	*kf = it->get();
			unsigned	c;

			if (!kf->trackCoverage)
				continue;

			c = kf->getCov();
			(*os)	<< "[" << kf->getModName() << "] "
				<< kmod->getPrettyName(kf->function)
				<< " " << c << " / "
				<< kf->numInstructions << " ";

			if (c == 0) {
				(*os) << '\n';
				continue;
			}

			/* dump out hexadecimal coverage bitmap */
			(*os) << kf->getCovStr() << '\n';
		}
	}
private:
	const char	*fname;
	Executor	&exe;
	bool		ignoreConcrete;
};


#define DUMP_BR(x,y,z,s)	\
cl::opt<unsigned> Dump##x(y, cl::desc(s), cl::init(0));	\
class x##Timer : public Executor::Timer {	\
public:	\
	x##Timer(const char* _fname, Executor &_exe)	\
	: fname(_fname), exe(_exe) {}	\
	void run(void) override {	\
		ExecutionState*	es;	\
\
		es = exe.getCurrentState();	\
		if (es == NULL) return;	\
\
		auto os = exe.getInterpreterHandler()->openOutputFile(fname);	\
		if (os == NULL) return;	\
\
		es->getBrTracker().z(*os);	\
	}	\
private:	\
	const char	*fname;	\
	Executor	&exe;	};


DUMP_BR(BTrackerDot, "dump-btracker-dot", dumpDotFile, "Dump branch dot tree")
DUMP_BR(BTrackerPaths, "dump-btracker-paths", dumpPathsFile, "Dump branch paths")


cl::opt<unsigned>
DumpFuncHeat("dump-func-heat",
	cl::desc("Dump function entry/exit counts (0=off)"),
	cl::init(0));
class FuncHeatTimer : public Executor::Timer
{
public:
	FuncHeatTimer(const char* _fname, Executor &_exe)
	: fname(_fname), exe(_exe) {}

	void run(void) override {
		const KModule	*km;

		auto os = exe.getInterpreterHandler()->openOutputFile(fname);
		if (os == NULL) return;

		km = exe.getKModule();

		foreach (it, km->kfuncsBegin(), km->kfuncsEnd()) {
			const KFunction	*kf = it->get();

			if (kf->getNumEnters() == 0 && kf->getNumExits() == 0)
				continue;

			(*os)	<< km->getPrettyName(kf->function)
				<< ' ' << kf->getNumEnters()
				<< ' ' << kf->getNumExits()
				<< '\n';
		}
	}
private:
	const char	*fname;
	Executor	&exe;
};



#include "../Solver/HashSolver.h"
cl::opt<unsigned>
DumpHashStats("dump-hashstats",
        cl::desc("Dump hash stats every n seconds (0=off)"),
        cl::init(0));
class HashStatTimer : public StatTimer
{
public:
	HashStatTimer(Executor &_exe) : StatTimer(_exe, "hashstats.txt") {}
protected:
	void print(void) { *os
		<< HashSolver::getHits() << ' '
		<< HashSolver::getStoreHits() << ' '
		<< HashSolver::getMisses(); }
};

cl::opt<unsigned>
DumpStackStats("dump-stackstats",
        cl::desc("Dump stack stats every n seconds (0=off)"),
        cl::init(0));
class StackStatTimer : public StatTimer
{
public:
	StackStatTimer(Executor& _exe)
	: StatTimer(_exe, "stackstats.txt") {}
protected:
	void print(void)
	{
		getStackStats();
		*os << s_min << ' ' << s_max << ' ' << s_avg << ' ' << n;
	}

	void getStackStats(void)
	{
		s_min = ~0;
		s_max = 0;
		s_total = 0;
		n = 0;
		foreach (it, exe.beginStates(), exe.endStates()) {
			const ExecutionState	*es;
			unsigned		cur_depth;

			es = *it;
			cur_depth = es->getStackDepth();
			if (cur_depth > s_max) s_max = cur_depth;
			if (cur_depth < s_min) s_min = cur_depth;
			n++;
			s_total += cur_depth;
		}

		s_avg = (n) ? s_total / n : 0;
	}
private:
	unsigned	s_min, s_max, s_total, s_avg, n;
};

#include "ForkHistory.h"
cl::opt<unsigned>
DumpForkCondGraph("dump-forkcondgraph",
	cl::desc("Dump fork condition graph (0=off)"),
	cl::init(0));

class ForkCondTimer : public Executor::Timer
{
public:
	ForkCondTimer(Executor &_exe) : exe(_exe) {}
	virtual ~ForkCondTimer() {}

	std::string cutoffExpr(const ref<Expr>& e)
	{
		std::string		ret;
		std::stringstream	ss;

		ss << e; 
		ret = ss.str(); 
		ss.str("");
		for (unsigned i = 0; i < ret.size(); i++) {
			if (ret[i] == '\n') {
				ss << "\\n";
				continue;
			}

			ss << ret[i];
		}
		ret = ss.str();
		if (ret.size() > 96) {
			ss.str("");
			ss << ret.substr(0, 96) << "\\n...";
			// << "\\n[" << e->hash() << "]";
			ret = ss.str();
		}

		return ret;
	}

	void run(void) override {
		auto os = exe.getInterpreterHandler()->openOutputFile(
			"fconds.txt");
		if (os == NULL) return;

		(*os) << "digraph {\n";
		for (const auto &p : exe.getForkHistory().conds()) {
			std::string 	from = cutoffExpr(p.first),
					to = cutoffExpr(p.second);
			(*os) << '"'<< from << "\" -> \"" << to << "\";\n";
		}
		(*os) << "\n}\n";
	}
private:
	Executor &exe;
};


cl::opt<unsigned>
DumpStateInstStats("dump-stateinststats",
        cl::desc("Dump state inst stats every n seconds (0=off)"),
        cl::init(0));
class StateInstStatTimer : public PyStatTimer
{
public:
	StateInstStatTimer(Executor &_exe) : PyStatTimer(_exe, "sinst.txt") {}
protected:
	void print(void)
	{
		std::map<unsigned, unsigned>	inst_buckets;

		foreach (it, exe.beginStates(), exe.endStates()) {
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
	UserCommand(Executor &_exe) : exe(_exe) {}
	virtual ~UserCommand() {}
	virtual void run(std::istream& is, std::ostream& os) = 0;
protected:
	Executor	&exe;
};

#define DECL_PIPECMD(x)			\
class PC##x : public UserCommand {	\
public:					\
	PC##x(Executor& _exe) : UserCommand(_exe) {}		\
	void run(std::istream &is, std::ostream& os) override;	\
};					\
void PC##x::run(std::istream& is, std::ostream& os)	\


DECL_PIPECMD(BacktraceStates)
{
	auto of = exe.getInterpreterHandler()->openOutputFile("tr.txt");
	if (of == NULL) {
		os << "[Backtrace] Couldn't open tr.txt!\n";
		return;
	}

	foreach (it, exe.beginStates(), exe.endStates()) {
		exe.printStackTrace(*(*it), *of);
	}
}

class UserCommandTimer : public Executor::Timer
{
public:
	UserCommandTimer(Executor &_exe) : exe(_exe), fd(-1)
	{
		cmds["backtrace"] = new PCBacktraceStates(exe);
	}
	virtual ~UserCommandTimer()
	{
		for (auto &p : cmds) delete p.second;
		close(fd);
	}

	void run(void) override;

private:
	typedef std::map<std::string, UserCommand*> cmds_ty;
	UserCommand* get(const char* buf)
	{
		cmds_ty::const_iterator it(cmds.find(buf));
		if (it == cmds.end())
			return NULL;
		return it->second;
	}

	Executor	&exe;
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



cl::opt<unsigned>
DumpPTreeTimer(
	"dump-ptree-timer",
        cl::desc("Periodically dump ptree information"));
class PTreeTimer : public Executor::Timer
{
public:
	PTreeTimer(Executor &_exe) : exe(_exe) {}

	void run()
	{
		char name[32];
		sprintf(name, "ptree%08d.dot", (int) stats::instructions);
		auto ih = exe.getInterpreterHandler();
		auto os = ih->openOutputFile(name);
		if (os) exe.getStateManager()->getPTree()->dump(*os);
	}
private:
	Executor	&exe;
};

cl::opt<unsigned>
DumpStatesTimer(
	"dump-states-timer",
        cl::desc("Periodically dump state information"));
class StatesTimer : public Executor::Timer
{
public:
	StatesTimer(Executor &_exe) : exe(_exe) {}

	void run()
	{
		auto ih = exe.getInterpreterHandler();
		auto os = ih->openOutputFile("states.txt");
		if (!os) return;
		for (auto es : *exe.getStateManager()) {
			dumpState(*os, es);
		}
	}

private:
	void dumpState(std::ostream& os, const ExecutionState* es) {
		os << "(" << es << ",";
		os << "[";
		auto next = es->stack.begin();
		++next;
		for (const auto &sf : es->stack) {
			os << "('" << sf.kf->function->getName().str() << "',";
			if (next == es->stack.end()) {
				os << es->prevPC->getInfo()->line << "), ";
			} else {
				os << next->caller->getInfo()->line << "), ";
				++next;
			}
		}
		os << "], ";

		const StackFrame &sf = es->stack.back();
		uint64_t md2u, icnt;

		md2u = computeMinDistToUncovered(
			es->pc,
			sf.minDistToUncoveredOnReturn);
		icnt = theStatisticManager->getIndexedValue(
			stats::instructions,
			es->pc->getInfo()->id);
		os << "{";
		os << "'depth' : " << es->depth << ", ";
		os << "'weight' : " << es->weight << ", ";
		os << "'queryCost' : " << es->queryCost << ", ";
		os << "'coveredNew' : " << es->coveredNew << ", ";
		os << "'instsSinceCovNew' : " <<
			es->lastNewInst-es->personalInsts << ", ";
		os << "'md2u' : " << md2u << ", ";
		os << "'icnt' : " << icnt << ", ";
		os << "}";
		os << ")\n";
	}

	Executor	&exe;
};

///

static const double kSecondsPerCheck = 0.25;

void Executor::initTimers(void)
{
	static int inited = 0;
	/* only initialize timers once */
	if (inited /*!timers.empty() */) return;
	inited++;

	if (OOMTimer::getMaxMemory())
		addTimer(std::make_unique<OOMTimer>(*this), 1.0);

	EXE_ADD_TIMER(HaltTimer, MaxTime);
	EXE_ADD_TIMER(HaltNoProgressTimer, MaxTimeNoProgress);
	EXE_ADD_TIMER(RuleBuilderStatTimer, DumpRuleBuilderStats)
	EXE_ADD_TIMER(MemStatTimer, DumpMemStats)
	EXE_ADD_TIMER(StateStatTimer, DumpStateStats)
	EXE_ADD_TIMER(ExprStatTimer, DumpExprStats)
	EXE_ADD_TIMER(CacheStatTimer, DumpCacheStats)
	EXE_ADD_TIMER(HashStatTimer, DumpHashStats)
	EXE_ADD_TIMER(CovStatTimer, DumpCovStats)
	EXE_ADD_TIMER(QueryStatTimer, DumpQueryStats)
	EXE_ADD_TIMER(StackStatTimer, DumpStackStats)
	EXE_ADD_TIMER(ExprGCTimer, UseGCTimer)
	EXE_ADD_TIMER(ExprObjScanTimer, UseObjScanTimer)
	EXE_ADD_TIMER(StateInstStatTimer, DumpStateInstStats)
	EXE_ADD_TIMER(ForkCondTimer, DumpForkCondGraph)
	EXE_ADD_TIMER(StatesTimer, DumpStatesTimer)
	EXE_ADD_TIMER(PTreeTimer, DumpPTreeTimer)

	EXE_ADD_TIMER2(CovTimer, DumpCovData, "cov.txt");
	EXE_ADD_TIMER2(BrDataTimer, DumpBrData, "brdata.txt");

	EXE_ADD_TIMER2(BTrackerDotTimer, DumpBTrackerDot, "btracker.dot");
	EXE_ADD_TIMER2(BTrackerPathsTimer, DumpBTrackerPaths, "btracker.path");
	EXE_ADD_TIMER2(FuncHeatTimer, DumpFuncHeat, "funcheat.txt");

	if (UseCmdUser) EXE_ADD_TIMER(UserCommandTimer, 2);

	if (DumpBrExprData)
		addTimer(std::make_unique<BrDataTimer>("brexprdata.txt", *this, true),
			 DumpBrExprData);
}

///


void Executor::addTimer(std::unique_ptr<Timer> timer, double rate) {
	std::cerr << "GOT RATE: " << rate << '\n';
	timers.push_back(std::make_unique<TimerInfo>(std::move(timer), rate));
}

Executor::TimerInfo::TimerInfo(std::unique_ptr<Timer> _timer, double _rate)
	: timer(std::move(_timer))
	, rate(_rate)
	, realRate(_rate)
	, nextFireTime(util::estWallTime() + rate)
{}

void Executor::TimerInfo::forceFire(double now)
{
	double after;
	timer->run();
	after = util::estWallTime();
	// adaptive rates for slower timers
	if ((after - now) > realRate) {
		std::cerr	<< "[ExeTimer] backing off from "
				<< realRate << " (base " << rate << ")\n";
		realRate += rate;
	} else if (realRate > rate) {
		realRate -= 0.5 * rate;
	} else {
		realRate = rate;
	}
	nextFireTime = realRate + after;
}

void Executor::processTimers(ExecutionState& current, double maxInstTime)
{
	static double lastCall = 0., lastCheck = 0.;
	double now = util::estWallTime();

	if (now - lastCheck <= kSecondsPerCheck) goto done;

	if (	maxInstTime > 0 &&
		!stateManager->isRemovedState(&current) &&
		lastCall != 0. && (now - lastCall) > maxInstTime)
	{
		klee_warning(
			"max-instruction-time exceeded: %.2fs",
			now - lastCall);
		TERMINATE_EARLY(this, current, "max-inst-time exceeded");
	}

	for (auto &ti : timers) ti->fire(now);

	lastCheck = now;

done:
	lastCall = now;
}

void Executor::flushTimers(void)
{
	double now = util::estWallTime();
	std::cerr << "[Exe] Flushing timers\n";
	for (auto &ti : timers) ti->forceFire(now);
}

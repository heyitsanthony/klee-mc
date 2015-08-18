//===-- StatsTracker.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "StatsTracker.h"

#include "klee/Common.h"
#include "klee/ExecutionState.h"
#include "klee/SolverStats.h"
#include "klee/Statistics.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/ModuleUtil.h"
#include "klee/Internal/Support/Stream.h"
#include "klee/Internal/System/Time.h"
#include "static/Sugar.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "CoreStats.h"
#include "Executor.h"
#include "ExeStateManager.h"
#include "MemUsage.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/CallSite.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Process.h>

#include <iostream>
#include <fstream>

using namespace klee;
using namespace llvm;

namespace
{
	cl::opt<bool> TrackInstructionTime(
		"track-instruction-time",
		cl::desc("Enable tracking of time for individual instructions"),
		cl::init(false));

	cl::opt<bool> OutputStats(
		"output-stats",
		cl::desc("Write running stats trace file"),
		cl::init(true));

	cl::opt<bool> OutputIStats(
		"output-istats",
		cl::desc("Write instruction level statistics (in callgrind format)"),
		cl::init(false));
	cl::opt<bool> TrackIStats(
		"track-istats",
		cl::desc("Track instruction level statistics"),
		cl::init(true));

	cl::opt<double> StatsWriteInterval(
		"stats-write-interval",
		cl::desc("Approximate number of seconds between stats writes (default: 1.0)"),
		cl::init(1.));

	cl::opt<double> IStatsWriteInterval(
	"istats-write-interval",
	cl::desc("Approximate number of seconds between istats writes (default: 10.0)"),
	cl::init(10.));

	// XXX I really would like to have dynamic rate control for something like this.
	cl::opt<double>
	UncoveredUpdateInterval("uncovered-update-interval", cl::init(30.));
}

StatsTracker* StatsTracker::create(
	Executor &_executor,
	const KModule* km,
	std::string _objectFilename,
	const std::vector<std::string> &excludeCovFiles)
{	// if (useStatistics())
	return new StatsTracker(
		_executor, km, _objectFilename, excludeCovFiles);
}

namespace klee {

bool StatsTracker::useStatistics() { return OutputStats || TrackIStats; }
#define DECL_STATTIMER(x,y)		\
class x##Timer : public Executor::Timer	{\
private:				\
	StatsTracker *statsTracker;	\
public:	\
	x##Timer(StatsTracker *_statsTracker)	\
	: statsTracker(_statsTracker) {}	\
	virtual ~x##Timer() {}	\
	void run() { statsTracker->y(); } };
DECL_STATTIMER(WriteIStats, writeIStats)
DECL_STATTIMER(WriteStats, writeStatsLine)
DECL_STATTIMER(UpdateReachable, computeReachableUncovered)
}


/// Check for special cases where we statically know an instruction is
/// uncoverable. Currently the case is an unreachable instruction
/// following a noreturn call; the instruction is really only there to
/// satisfy LLVM's termination requirement.
static bool instructionIsCoverable(Instruction *i)
{
	if (i->getOpcode() != Instruction::Unreachable)
		return true;

	BasicBlock *bb = i->getParent();
	BasicBlock::iterator it(i);
	if (it==bb->begin())
		return true;

	Instruction *prev = --it;
	if (!(isa<CallInst>(prev) || isa<InvokeInst>(prev))) {
		return true;
	}

	Function *target = getDirectCallTarget(prev);
	if (target && target->doesNotReturn())
		return false;

	return true;
}

StatsTracker::StatsTracker(
	Executor &_executor,
	const KModule	*in_km,
	std::string _objectFilename,
	const std::vector<std::string> &excludeCovFiles)
: executor(_executor)
, objectFilename(_objectFilename)
, startWallTime(util::estWallTime())
, numBranches(0)
, fullBranches(0)
, partialBranches(0)
, updateMinDistToUncovered(false)
, lastCoveredInstruction(0)
{
	std::string	module(objectFilename);
	km = in_km;

	if (objectFilename[0] != '/') {
		char	cwd[PATH_MAX];
		struct stat s;
		getcwd(cwd, PATH_MAX);
		sprintf(cwd, "%s/%s", cwd, objectFilename.c_str());
		if (stat(cwd, &s) != -1)
			objectFilename = cwd;
	}

	for (auto &f_s : excludeCovFiles) {
		std::ifstream ifs(f_s.c_str());
		if (!ifs.good())
			klee_error("file not found: %s", f_s.c_str());

		std::string line;
		while (!ifs.eof()) {
			std::getline(ifs, line);
			// strip '\x01' sentinels
			if(line[0] == 1)
				line = line.substr(1);
			excludeNames.insert(line);
		}
	}

	foreach (it, km->kfuncsBegin(), km->kfuncsEnd())
		addKFunction(it->get());

	if (OutputStats) {
		statsFile = executor.interpreterHandler->openOutputFile(
			"run.stats");

		assert(statsFile && "unable to open statistics trace file");
		writeStatsLine();

		executor.addTimer(
			std::make_unique<WriteStatsTimer>(this),
			StatsWriteInterval);
	}

	if (OutputIStats) {
		istatsFile = executor.interpreterHandler->openOutputFile(
			"run.istats");
		assert(istatsFile && "unable to open istats file");
		executor.addTimer(
			std::make_unique<WriteIStatsTimer>(this),
			IStatsWriteInterval);
	}
}

void StatsTracker::setUpdateMinDist(void)
{
	if (updateMinDistToUncovered) return;

	updateMinDistToUncovered = true;

	computeReachableUncovered();
	executor.addTimer(
		std::make_unique<UpdateReachableTimer>(this),
		UncoveredUpdateInterval);
}

void StatsTracker::done()
{
	if (statsFile) writeStatsLine();
	if (OutputIStats) writeIStats();
}


void StatsTracker::addKFunction(KFunction* kf)
{
	const std::string &name = kf->function->getName();
	size_t lastNondigit = name.find_last_not_of("0123456789");

	if (kf->trackCoverage) {
		kf->trackCoverage =
			!(excludeNames.count(name) ||
			excludeNames.count(name.substr(0, lastNondigit+1)));
	}

	for (unsigned i=0; i<kf->numInstructions; ++i) {
		KInstruction *ki = kf->instructions[i];

		if (TrackIStats) {
			unsigned id = ki->getInfo()->id;
			theStatisticManager->setIndex(id);
		}

		if (kf->trackCoverage) {
			if (instructionIsCoverable(ki->getInst()))
				++stats::uncoveredInstructions;
			if (BranchInst *bi = dyn_cast<BranchInst>(ki->getInst()))
				if (!bi->isUnconditional())
					numBranches++;
		}
	}

	if (!init) {
		/* hack to get dynamic adding kfuncs. Slightly wrong. */
		std::vector<Instruction*>	iv;
		computeCallTargets(kf->function);
		initMinDistToReturn(kf->function, iv);
	}
}

void StatsTracker::trackInstTime(ExecutionState& es)
{
	static sys::TimeValue lastNowTime(0,0), lastUserTime(0,0);

	if (lastUserTime.seconds()==0 && lastUserTime.nanoseconds()==0) {
		sys::TimeValue sys(0,0);
		sys::Process::GetTimeUsage(lastNowTime,lastUserTime,sys);
		return;
	}

	sys::TimeValue now(0,0),user(0,0),sys(0,0);
	sys::Process::GetTimeUsage(now,user,sys);
	sys::TimeValue delta = user - lastUserTime;
	sys::TimeValue deltaNow = now - lastNowTime;
	stats::instructionTime += delta.usec();
	stats::instructionRealTime += deltaNow.usec();
	lastUserTime = user;
	lastNowTime = now;
}

void StatsTracker::stepInstUpdateFrame(ExecutionState &es)
{
	if (es.stack.empty())
		return;

	const StackFrame	&sf(es.stack.back());
	const InstructionInfo	&ii(*es.pc->getInfo());


	theStatisticManager->setIndex(ii.id);
	if (es.pc->isCovered())
		return;

	Instruction *inst = es.pc->getInst();
	if (!sf.kf->trackCoverage || !instructionIsCoverable(inst))
		return;

	// Checking for actual stoppoints avoids inconsistencies due
	// to line number propogation.
	if (!ii.file.empty())
		es.coveredLines[&ii.file].insert(ii.line);

	lastCoveredInstruction = stats::instructions+1;
	es.coveredNew = true;
	es.lastNewInst = es.totalInsts;

	es.pc->cover(es.getSID());
	++stats::coveredInstructions;
	es.newInsts++;
	stats::uncoveredInstructions += (int64_t)-1;
	assert ((int64_t)stats::uncoveredInstructions >= 0);
}

void StatsTracker::stepInstruction(ExecutionState &es)
{
	if (!TrackIStats) return;

	if (TrackInstructionTime)
		trackInstTime(es);

	stepInstUpdateFrame(es);
}

///

/* Should be called _after_ the es->pushFrame() */
void StatsTracker::framePushed(ExecutionState &es, StackFrame *parentFrame)
{
	if (!TrackIStats) return;

	StackFrame &sf = es.stack.back();

	if (updateMinDistToUncovered) {
		uint64_t minDistAtRA = 0;
		if (parentFrame)
			minDistAtRA = parentFrame->minDistToUncoveredOnReturn;

		sf.minDistToUncoveredOnReturn = sf.caller
			? computeMinDistToUncovered(sf.caller, minDistAtRA)
			: 0;
	}
}

/* Should be called _after_ the es->popFrame() */
void StatsTracker::framePopped(ExecutionState &es)
{
	// XXX remove me?
}


void StatsTracker::markBranchVisited(
	KBrInstruction	*kbr,
	ExecutionState	*visitedTrue,
	ExecutionState	*visitedFalse)
{
	uint64_t hasTrue, hasFalse;

	if (!TrackIStats)
		return;

	hasTrue = kbr->hasFoundTrue();
	hasFalse = kbr->hasFoundFalse();

	if (visitedTrue && !hasTrue) {
		visitedTrue->coveredNew = true;
		visitedTrue->lastNewInst = visitedTrue->totalInsts;
		++stats::trueBranches;
		if (hasFalse) {
			++fullBranches;
			--partialBranches;
		} else
			++partialBranches;

		hasTrue = true;
	}

	if (visitedFalse && !hasFalse) {
		visitedFalse->coveredNew = true;
		visitedFalse->lastNewInst =  visitedFalse->totalInsts;
		++stats::falseBranches;
		if (hasTrue) {
			++fullBranches;
			--partialBranches;
		} else
			++partialBranches;
	}
}

class StatsTracker::TimeAmountFormat
{
	double v;
public:
	TimeAmountFormat(double v0) : v(v0) { }
	friend std::ostream& operator<<(std::ostream& os, TimeAmountFormat t)
	{
		util::IosStateSaver iss(os);
		os.setf(std::ios_base::fixed, std::ios_base::floatfield);
		os.precision(6);
		os << t.v;
		return os;
	}
};

double StatsTracker::elapsed() { return util::estWallTime() - startWallTime; }

static const char* stat_labels[] =
{
	"Instructions",
	"FullBranches",
	"PartialBranches",
	"NumBranches",
	"UserTime",
	"NumStates",
	"NumStatesNC",
	"MemUsedKB",
	"NumQueries",
	"NumQueryConstructs",
	"NumObjects",
	"WallTime",
	"CoveredInstructions",
	"UncoveredInstructions",
	"QueryTime",
	"SolverTime",
	"CexCacheTime",
	"ForkTime",
	"ResolveTime",
	"Forks",
};

void StatsTracker::writeStatsLine()
{
#define WRITE_LABEL(x)	*statsFile << "'" << stat_labels[i++] << "' : " << x << ", ";
	int	i = 0;
	*statsFile << "{ ";
	WRITE_LABEL(stats::instructions)
	WRITE_LABEL(fullBranches)
	WRITE_LABEL(partialBranches)
	WRITE_LABEL(numBranches)
	WRITE_LABEL(TimeAmountFormat(util::getUserTime()))
	WRITE_LABEL(executor.stateManager->size())
	WRITE_LABEL(executor.stateManager->getNonCompactStateCount())
	WRITE_LABEL(getMemUsageKB())
	WRITE_LABEL(stats::queries)
	WRITE_LABEL(stats::queryConstructs)
	WRITE_LABEL(0 /* NumObjects */)
	WRITE_LABEL(TimeAmountFormat(elapsed()))
	WRITE_LABEL(stats::coveredInstructions)
	WRITE_LABEL(stats::uncoveredInstructions)
	WRITE_LABEL(TimeAmountFormat(stats::queryTime / 1.0e6))
	WRITE_LABEL(TimeAmountFormat(stats::solverTime / 1.0e6))
	WRITE_LABEL(TimeAmountFormat(stats::cexCacheTime / 1.0e6))
	WRITE_LABEL(TimeAmountFormat(stats::forkTime / 1.0e6))
	WRITE_LABEL(TimeAmountFormat(stats::resolveTime / 1.0e6))
	WRITE_LABEL(stats::forks)

	*statsFile << "}\n";
	statsFile->flush();
}

void StatsTracker::updateStateStatistics(uint64_t addend)
{
	for (auto &st_p : *executor.stateManager) {
		ExecutionState &state = *st_p;
		const InstructionInfo &ii = *state.pc->getInfo();
		theStatisticManager->incrementIndexedValue(
			stats::states, ii.id, addend);
	}
}

void StatsTracker::writeIStats(void)
{
  Module *m = km->module.get();
  uint64_t istatsMask = 0;
  std::ostream &of = *istatsFile;

  of.seekp(0, std::ios::end);
  unsigned istatsSize = of.tellp();
  of.seekp(0);

  of << "version: 1\n";
  of << "creator: klee\n";
  of << "pid: " << getpid() << '\n';
  of << "cmd: " << m->getModuleIdentifier() << "\n\n";
  of << "\n";

  StatisticManager &sm = *theStatisticManager;
  unsigned nStats = sm.getNumStatistics();

  // Max is 13, sadly
  istatsMask |= 1<<sm.getStatisticID("Queries");
  istatsMask |= 1<<sm.getStatisticID("QueriesValid");
  istatsMask |= 1<<sm.getStatisticID("QueriesInvalid");
  istatsMask |= 1<<sm.getStatisticID("QueryTime");
  istatsMask |= 1<<sm.getStatisticID("ResolveTime");
  istatsMask |= 1<<sm.getStatisticID("Instructions");
  istatsMask |= 1<<sm.getStatisticID("InstructionTimes");
  istatsMask |= 1<<sm.getStatisticID("InstructionRealTimes");
  istatsMask |= 1<<sm.getStatisticID("Forks");
  istatsMask |= 1<<sm.getStatisticID("CoveredInstructions");
  istatsMask |= 1<<sm.getStatisticID("UncoveredInstructions");
  istatsMask |= 1<<sm.getStatisticID("States");
  istatsMask |= 1<<sm.getStatisticID("MinDistToUncovered");

  of << "positions: instr line\n";

  for (unsigned i=0; i<nStats; i++) {
    if (istatsMask & (1<<i)) {
      Statistic &s = sm.getStatistic(i);
      of << "event: " << s.getShortName() << " : "
         << s.getName() << "\n";
    }
  }

  of << "events: ";
  for (unsigned i=0; i<nStats; i++) {
    if (istatsMask & (1<<i))
      of << sm.getStatistic(i).getShortName() << " ";
  }
  of << "\n";

  // set state counts, decremented after we process so that we don't
  // have to zero all records each time.
  if (istatsMask & (1<<stats::states.getID()))
    updateStateStatistics(1);

  std::string sourceFile = "";

  of << "ob=" << objectFilename << "\n";

  foreach (fnIt, m->begin(), m->end()) {
    if (fnIt->isDeclaration()) continue;
    foreach (bbIt, fnIt->begin(), fnIt->end()) {
      foreach (it, bbIt->begin(), bbIt->end())
      	writeInstIStat(of, istatsMask, sourceFile, &*it);
    }
  }

  if (istatsMask & (1<<stats::states.getID()))
    updateStateStatistics((uint64_t)-1);

  // Clear then end of the file if necessary (no truncate op?).
  unsigned pos = of.tellp();
  for (unsigned i=pos; i<istatsSize; ++i)
    of << '\n';

  of.flush();
}

void StatsTracker::writeInstIStat(
	std::ostream& of,
	uint64_t istatsMask,
	std::string& sourceFile,
	llvm::Instruction *instr)
{
	StatisticManager &sm = *theStatisticManager;
	unsigned nStats = sm.getNumStatistics();
	const InstructionInfo	&ii(km->infos->getInfo(instr));
	unsigned		index = ii.id;

	if (ii.file!=sourceFile) {
		if(ii.file.empty())
			of << "fl=[klee]\n";
		else
			of << "fl=" << ii.file << "\n";
		sourceFile = ii.file;
	}

	if (	instr == instr->getParent()->begin() &&
		instr->getParent() == instr->getParent()->getParent()->begin())
	{
		of << "fn=" << instr->getParent()->
			getParent()->getName().str() << "\n";
	}
	of << ii.assemblyLine << " ";
	of << ii.line << " ";

	for (unsigned i=0; i<nStats; i++)
		if (istatsMask&(1<<i))
			of << sm.getIndexedValue(
				sm.getStatistic(i), index) << " ";
	of << "\n";
}

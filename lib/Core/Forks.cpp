#include <llvm/Support/CommandLine.h>
#include <llvm/IR/Instruction.h>
#include <iostream>
#include <sstream>

#include "klee/Statistics.h"
#include "klee/Internal/ADT/RNG.h"
#include "StatsTracker.h"
#include "CoreStats.h"
#include "OOMTimer.h"

#include "klee/TimerStatIncrementer.h"
#include "static/Sugar.h"

#include "StateSolver.h"

#include "PTree.h"
#include "ExeStateManager.h"
#include "Forks.h"

/* XXX */
#include "../Solver/SMTPrinter.h"
#include "klee/Internal/ADT/LimitedStream.h"


namespace klee { extern RNG theRNG; }

namespace
{
	llvm::cl::opt<double>
		MaxStaticForkPct("max-static-fork-pct", llvm::cl::init(1.));
	llvm::cl::opt<double>
		MaxStaticSolvePct("max-static-solve-pct", llvm::cl::init(1.));
	llvm::cl::opt<double>
		MaxStaticCPForkPct("max-static-cpfork-pct", llvm::cl::init(1.));
	llvm::cl::opt<double>
	MaxStaticCPSolvePct("max-static-cpsolve-pct", llvm::cl::init(1.));
	llvm::cl::opt<unsigned>
		MaxDepth("max-depth",
			llvm::cl::desc("Only this many sym branches (0=off)"),
			llvm::cl::init(0));

	llvm::cl::opt<bool>
	MaxMemoryInhibit(
		"max-memory-inhibit",
		llvm::cl::desc("Stop forking at memory cap (vs. random kill)"),
		llvm::cl::init(true));

	llvm::cl::opt<bool>
	ReplayInhibitedForks(
		"replay-inhibited-forks",
		llvm::cl::desc("Replay fork inhibited path as new state"),
		llvm::cl::init(true));

	llvm::cl::opt<unsigned>
	MaxForks(
		"max-forks",
		llvm::cl::desc("Only fork this many times (-1=off)"),
		llvm::cl::init(~0u));

	llvm::cl::opt<bool>
	RandomizeFork("randomize-fork", llvm::cl::init(true));

	/* this is kind of useful for limiting useless test cases but
	 * tends to drop good states */
	llvm::cl::opt<bool>
	QuenchRunaways(
		"quench-runaways",
		llvm::cl::desc("Drop states at heavily forking instructions."),
		llvm::cl::init(false));
}

using namespace klee;

unsigned Forks::quench_c = 0;
unsigned Forks::fork_c = 0;
unsigned Forks::fork_uniq_c = 0;
Forks::xfer_f_t Forks::transition_f = [] (const auto& fi) {};

#define RUNAWAY_REFRESH	32
bool Forks::isRunawayBranch(KInstruction* ki)
{
	KBrInstruction	*kbr;
	double		stddevs;
	static int	count = 0;
	static double	stddev, mean, median;
	unsigned	forks, rand_mod;

	kbr = dynamic_cast<KBrInstruction*>(ki);
	if (kbr == NULL) {
		bool		is_likely;
		unsigned	mean;
		unsigned 	r;

		/* only rets and branches may be runaways */
		if (ki->getInst()->getOpcode() != llvm::Instruction::Ret)
			return false;

		/* XXX: FIXME. Need better scoring system. */
		mean = (fork_c+fork_uniq_c-1)/fork_uniq_c;
		r = rand() % (1 << (1+(10*ki->getForkCount())/mean));
		is_likely = (r != 0);
		return is_likely;
	}

	if ((count++ % RUNAWAY_REFRESH) == 0) {
		stddev = KBrInstruction::getForkStdDev();
		mean = KBrInstruction::getForkMean();
		median = KBrInstruction::getForkMedian();
	}

	if (stddev == 0)
		return false;

	forks = kbr->getForkHits();
	if (forks <= 5)
		return false;

	stddevs = ((double)(kbr->getForkHits() - mean))/stddev;
	if (stddevs < 1.0)
		return false;

	rand_mod = (1 << (1+(int)(((double)forks/(median)))));
	if ((theRNG.getInt31() % rand_mod) == 0)
		return false;

	return true;
}

StatePair
Forks::fork(ExecutionState &s, ref<Expr> cond, bool isInternal)
{
	StateVector	results;
	std::vector<ref<Expr>>	conds(2);

	// set in forkSetupNoSeeding, if possible
	//  conditions[0] = Expr::createIsZero(condition);
	conds[1] = cond;

	results = fork(s, conds, isInternal, true);
	lastFork = std::make_pair(
		results[1] /* first label in br => true */,
		results[0] /* second label in br => false */);

	return lastFork;
}

StatePair Forks::forkUnconditional(
	ExecutionState &s, bool isInternal)
{
	std::vector<ref<Expr>>	conds(2);
	StateVector	results;

	conds[0] = MK_CONST(1, 1);
	conds[1] = MK_CONST(1, 1);

	/* NOTE: not marked as branch so that branch optimization is not
	 * applied-- giving it (true, true) would result in only one branch! */
	results = fork(s, conds, isInternal, false);
	lastFork = std::make_pair(
		results[1] /* first label in br => true */,
		results[0] /* second label in br => false */);
	return lastFork;
}

void ForkInfo::dump(std::ostream& os) const
{
	os << "ForkInfo: {\n";
	for (unsigned i = 0; i < size(); i++) {
		if (conditions[i].isNull())
			continue;
		os << "COND[" << i << "]: "  << conditions[i] << '\n';
	}

	for (unsigned i = 0; i < res.size(); i++)
		os << "res[" << i << "]: " << res[i] << '\n';

	os << '\n';
}

bool Forks::forkSetup(ExecutionState& current, struct ForkInfo& fi)
{
	if (!fi.isInternal && current.isCompact()) {
		// Can't fork compact states; sanity check
		assert(false && "invalid state");
	}

	if (fi.validTargets <= 1)  return true;

	// Multiple branch directions are possible; check for flags that
	// prevent us from forking here
	assert(	!exe.getReplayKTest() &&
		"in ktest replay mode, only one branch can be true.");

	if (fi.isInternal) return true;

	const char* reason = 0;
	if (MaxMemoryInhibit && OOMTimer::isAtMemoryLimit())
		reason = "memory cap exceeded";
//	if (current.forkDisabled)
//		reason = "fork disabled on current path";
	if (MaxForks!=~0u && stats::forks >= MaxForks)
		reason = "max-forks reached";

	if (reason == NULL)
		return true;

	if (ReplayInhibitedForks) {
		klee_warning_once(
			reason,
			"forking into compact forms (%s)",
			reason);
		fi.forkCompact = true;
		return true;
	}

	// Skipping fork for one of above reasons; randomly pick target
	skipAndRandomPrune(fi, reason);
	return true;
}

void Forks::skipAndRandomPrune(struct ForkInfo& fi, const char* reason)
{
	TimerStatIncrementer timer(stats::forkTime);
	unsigned randIndex, condIndex;

	klee_warning_once(
		reason,
		"skipping fork and pruning randomly (%s)",
		reason);

	randIndex = (theRNG.getInt32() % fi.validTargets) + 1;
	for (condIndex = 0; condIndex < fi.size(); condIndex++) {
		if (fi.res[condIndex]) randIndex--;
		if (!randIndex) break;
	}

	assert(condIndex < fi.size());
	fi.validTargets = 1;
	fi.res.assign(fi.size(), false);
	fi.res[condIndex] = true;
}


// !!! for normal branch, conditions = {false,true} so that replay 0,1 reflects
// index
StateVector
Forks::fork(
	ExecutionState &current,
	const std::vector<ref<Expr>> &conditions,
        bool isInternal,
	bool isBranch)
{
	ForkInfo	fi(conditions);

	/* limit runaway branches */
	fi.forkDisabled = current.forkDisabled;
	if (	is_quench &&
		!fi.forkDisabled &&
		(*current.prevPC).getForkCount() > 10)
	{
		fi.forkDisabled = isRunawayBranch(current.prevPC);
	}

	fi.isInternal = isInternal;
	fi.isBranch = isBranch;

	/* find feasible forks */
	if (evalForks(current, fi) == false) {
		if (fi.size())
			TERMINATE_EARLY(&exe, current, "fork query timed out");
		return StateVector(fi.size(), NULL);
	}

	// need a copy telling us whether or not we need to add
	// constraints later; especially important if we skip a fork for
	// whatever reason
	fi.feasibleTargets = fi.validTargets;
	assert(fi.validTargets && "invalid set of fork conditions");

	if (forkSetup(current, fi) == false)
		return StateVector(fi.size(), NULL);

	if (makeForks(current, fi) == false)
		return StateVector(fi.size(), NULL);

	constrainForks(current, fi);

	if (fi.forkedTargets) {
		if (current.prevPC->getForkCount() == 0)
			fork_uniq_c++;
		current.prevPC->forked();
		fork_c += fi.forkedTargets;
	} else if (fi.validTargets > 1 && fi.forkDisabled) {
		quench_c++;
	}

	return fi.resStates;
}

bool Forks::evalForkBranch(ExecutionState& s, struct ForkInfo& fi)
{
	Solver::Validity	result;
	bool			ok;

	assert (fi.isBranch);

	ok = exe.getSolver()->evaluate(s, fi.conditions[1], result);
	if (!ok)
		return false;

	// known => [0] when false, [1] when true
	// unknown => take both routes
	fi.res[0] = (
		result == Solver::False ||
		result == Solver::Unknown);
	fi.res[1] = (
		result == Solver::True ||
		result == Solver::Unknown);

	fi.validTargets = (result == Solver::Unknown) ? 2 : 1;
	if (fi.validTargets > 1 && fi.conditions[0].isNull()) {
		/* branch on both true and false conditions */
		fi.conditions[0] = Expr::createIsZero(fi.conditions[1]);
	}

	return true;
}

// Evaluate fork conditions
bool Forks::evalForks(ExecutionState& current, struct ForkInfo& fi)
{
	if (fi.isBranch) {
		return evalForkBranch(current, fi);
	}

	assert (fi.isBranch == false);

	for (unsigned int condIndex = 0; condIndex < fi.size(); condIndex++) {
		ConstantExpr	*CE;
		bool		result;

		CE = dyn_cast<ConstantExpr>(fi.conditions[condIndex]);
		// If condition is a constant
		// (e.g., from constant switch statement),
		// don't generate a query
		if (CE != NULL) {
			if (CE->isFalse()) result = false;
			else if (CE->isTrue()) result = true;
			else assert(false && "Invalid constant fork condition");
		} else {
			bool	ok;
			ok = exe.getSolver()->mayBeTrue(
				current,
				fi.conditions[condIndex],
				result);
			if (!ok)
				return false;
		}

		fi.res[condIndex] = result;
		if (result)
			fi.validTargets++;
	}

	return true;
}

ExecutionState* Forks::pureFork(ExecutionState& es, bool compact)
{
	// Update stats
	TimerStatIncrementer	timer(stats::forkTime);
	ExecutionState		*newState;

	++stats::forks;

	// Do actual state forking
	newState = es.branch(compact);
	es.ptreeNode->markReplay();

	// Update path tree with new states
	exe.getStateManager()->queueSplitAdd(es.ptreeNode, &es, newState);
	return newState;
}

bool Forks::setupForkAffinity(
	ExecutionState& current,
	const struct ForkInfo& fi,
	unsigned* cond_idx_map)
{
	if (preferFalseState) {
		/* swap true with false */
		cond_idx_map[0] = 1;
		cond_idx_map[1] = 0;
	} else if (preferTrueState) {
		/* nothing-- default prefers true */
	} else if (RandomizeFork) {
		for (unsigned i = 0; i < fi.size(); i++) {
			unsigned	swap_idx, swap_val;

			swap_idx = theRNG.getInt32() % fi.size();
			swap_val = cond_idx_map[swap_idx];
			cond_idx_map[swap_idx] = cond_idx_map[i];
			cond_idx_map[i] = swap_val;
		}
	}

	return true;
}

bool Forks::makeForks(ExecutionState& current, struct ForkInfo& fi)
{
	ExecutionState	**curStateUsed = NULL;
	unsigned	cond_idx_map[fi.size()];

	for (unsigned i = 0; i < fi.size(); i++)
		cond_idx_map[i] = i;

	if (setupForkAffinity(current, fi, cond_idx_map) == false)
		return false;

	for (unsigned int i = 0; i < fi.size(); i++) {
		ExecutionState	*newState, *baseState;
		unsigned	condIndex;

		condIndex = cond_idx_map[i];

		// Process each valid target and generate a state
		if (!fi.res[condIndex]) continue;

		if (!curStateUsed) {
			/* reuse current state, when possible */
			fi.resStates[condIndex] = &current;
			curStateUsed = &fi.resStates[condIndex];
			continue;
		}

		assert (!fi.forkCompact || ReplayInhibitedForks);

		if (fi.forkDisabled) {
			fi.res[condIndex] = false;
			continue;
		}

		// Do actual state forking
		baseState = &current;

		newState = pureFork(current, fi.forkCompact);
		fi.forkedTargets++;
		fi.resStates[condIndex] = newState;

		// Split pathWriter stream
		if (!fi.isInternal) {
			TreeStreamWriter	*tsw;

			tsw = exe.getSymbolicPathWriter();
			if (tsw != NULL && newState != baseState) {
				newState->symPathOS = tsw->open(
					current.symPathOS);
			}
		}
	}

	if (fi.validTargets >= 2 && !fi.forkDisabled)
		transition_f(fi);

	return true;
}

bool Forks::addConstraint(struct ForkInfo& fi, unsigned condIndex)
{
	ExecutionState	*curState;

	curState = fi.resStates[condIndex];

	if (curState->isCompact())
		return true;

	// nothing to fork
	if (fi.feasibleTargets == 0)
		return true;

	// must fork
	if (	fi.feasibleTargets > 1 || 
		(	omit_valid_constraints == false &&
			!fi.conditions[condIndex].isNull()))
	{
		if (exe.addConstraint(*curState, fi.conditions[condIndex]))
			return true;

		TERMINATE_EARLY(&exe, *curState, "branch contradiction");
		fi.resStates[condIndex] = NULL;
		fi.res[condIndex] = false;
		return false;
	}

	assert (fi.feasibleTargets == 1);
	if (fi.conditions[1]->getKind() == Expr::Constant)
		return true;

	/* even if we can ignore the constraint because it is
	 * implied by the former constraints, IVC can help if
	 * the constraint is more precise
	 * (e.g.  3<x<5 vs x==4) */

	ref<Expr>	cond(fi.conditions[condIndex]);
	if (	cond.isNull() &&
		condIndex == 0 &&
		!fi.conditions[1].isNull())
	{
		// XXX WTF??
		cond = Expr::createIsZero(fi.conditions[1]);
	}
	assert(!cond.isNull());

	exe.doImpliedValueConcretization(*curState, cond, MK_CONST(1, 1));
	return true;
}

bool Forks::constrainFork(
	ExecutionState& current,
	struct ForkInfo& fi,
	unsigned int condIndex)
{
	ExecutionState* curState;

	if (fi.res[condIndex] == false)
		return false;

	curState = fi.resStates[condIndex];
	assert(curState);

	// Add path constraint
	if (!addConstraint(fi, condIndex))
		return false;

	// XXX - even if the constraint is provable one way or the other we
	// can probably benefit by adding this constraint and allowing it to
	// reduce the other constraints. For example, if we do a binary
	// search on a particular value, and then see a comparison against
	// the value it has been fixed at, we should take this as a nice
	// hint to just use the single constraint instead of all the binary
	// search ones. If that makes sense.


	// Kinda gross, do we even really still want this option?
	if (MaxDepth && MaxDepth <= curState->depth) {
		TERMINATE_EARLY(&exe, *curState, "max-depth exceeded");
		fi.resStates[condIndex] = NULL;
		return false;
	}

	// Auxiliary bookkeeping
	if (!fi.isInternal) {
		if (exe.getSymbolicPathWriter() && fi.validTargets > 1) {
			std::stringstream ssPath;
			ssPath << condIndex << "\n";
			curState->symPathOS << ssPath.str();
		}
	}

	trackBranch(*curState, condIndex);
	return true;
}

void Forks::constrainForks(ExecutionState& current, struct ForkInfo& fi)
{
	// Loop for bookkeeping
	// (loops must be separate since states are forked from each other)
	for (unsigned int condIndex = 0; condIndex < fi.size(); condIndex++) {
		constrainFork(current, fi, condIndex);
	}
}

Forks::~Forks(void) { }

Forks::Forks(Executor& _exe)
: exe(_exe)
, suppressForks(Replay::isSuppressForks())
, preferTrueState(false)
, preferFalseState(false)
, lastFork(0,0)
, is_quench(QuenchRunaways)
, omit_valid_constraints(true)
{
}

void Forks::trackBranch(ExecutionState& current, unsigned condIndex)
{ current.trackBranch(condIndex, current.prevPC); }

bool Forks::isReplayInhibitedForks(void) { return ReplayInhibitedForks; }

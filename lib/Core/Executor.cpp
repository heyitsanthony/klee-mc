//===-- Executor.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#include "Executor.h"
#include "ExecutorBC.h"	/* for 'interpreter::create()' */
#include "ExeStateManager.h"

#include "Context.h"
#include "CoreStats.h"
#include "ImpliedValue.h"
#include "Memory.h"
#include "MemoryManager.h"
#include "PTree.h"
#include "Searcher.h"
#include "SeedInfo.h"
#include "TimingSolver.h"
#include "UserSearcher.h"
#include "MemUsage.h"
#include "StatsTracker.h"

#include "static/Sugar.h"
#include "klee/Common.h"
#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Interpreter.h"
#include "klee/SolverStats.h"
#include "klee/TimerStatIncrementer.h"
#include "klee/util/Assignment.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprUtil.h"
#include "klee/Config/config.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/Internal/ADT/RNG.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/FloatEvaluation.h"
#include "klee/Internal/System/Time.h"

#include "llvm/Attributes.h"
#include "llvm/BasicBlock.h"
#include "llvm/Constants.h"
#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Module.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/GetElementPtrTypeIterator.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetData.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

#include <sys/mman.h>
#include <netdb.h>

#include <errno.h>
#include <cxxabi.h>

using namespace llvm;
using namespace klee;

// omg really hard to share cl opts across files ...
bool WriteTraces = false;

static std::string expr2str(const Expr* e)
{
  std::ostringstream info;
  e->print(info);
  return info.str();
}

static std::string state2str(ExecutionState* state)
{
  std::ostringstream info;
  state->constraints.print(info);
  return info.str();
}

static std::string states2str(const ExeStateSet& ess)
{
  std::ostringstream s;
  unsigned int i = 0;
  for (ExeStateSet::const_iterator it = ess.begin();
      it != ess.end(); it++, i++)
  {
    s << "State " << i << ":\n";
    s << state2str(*it);
    s << "\n";
  }
  return s.str();
}

namespace llvm
{
  namespace cl
  {
    template <>
    class parser<sockaddr_in_opt> : public basic_parser<sockaddr_in_opt> {
    public:
      bool parse(llvm::cl::Option &O, const char *ArgName, const std::string &Arg, sockaddr_in_opt &Val);
      virtual const char *getValueName() const { return "sockaddr_in"; }
    };
  }
}

namespace {
  cl::opt<bool>
  DumpStatesOnHalt("dump-states-on-halt",
                   cl::init(true));

  cl::opt<bool>
  NoPreferCex("no-prefer-cex",
              cl::init(false));


  cl::opt<bool>
  RandomizeFork("randomize-fork",
                cl::init(false));

  cl::opt<bool>
  DebugPrintInstructions("debug-print-instructions",
                         cl::desc("Print instructions during execution."));

  cl::opt<bool>
  DebugCheckForImpliedValues("debug-check-for-implied-values");


  cl::opt<bool>
  SimplifySymIndices("simplify-sym-indices",
                     cl::init(false));

  cl::opt<unsigned>
  MaxSymArraySize("max-sym-array-size",
                  cl::init(0));

  cl::opt<bool>
  DebugValidateSolver("debug-validate-solver",
		      cl::init(false));

  cl::opt<bool>
  OnlyOutputStatesCoveringNew("only-output-states-covering-new",
                              cl::init(false));

  cl::opt<bool>
  AlwaysOutputSeeds("always-output-seeds",
                              cl::init(true));

  cl::opt<bool>
  UseFastCexSolver("use-fast-cex-solver",
		   cl::init(false));

  cl::opt<bool>
  UseIndependentSolver("use-independent-solver",
                       cl::init(true),
		       cl::desc("Use constraint independence"));

  cl::opt<bool>
  EmitAllErrors("emit-all-errors",
                cl::init(false),
                cl::desc("Generate tests cases for all errors "
                         "(default=one per (error,instruction) pair)"));

  cl::opt<bool>
  UseCexCache("use-cex-cache",
              cl::init(true),
	      cl::desc("Use counterexample caching"));

  cl::opt<bool>
  UseQueryPCLog("use-query-pc-log",
                cl::init(false));
 
  cl::opt<bool>
  UseSTPQueryPCLog("use-stp-query-pc-log",
                   cl::init(false));

  cl::opt<bool>
  UseCache("use-cache",
	   cl::init(true),
	   cl::desc("Use validity caching"));

  cl::opt<bool>
  OnlyReplaySeeds("only-replay-seeds",
                  cl::desc("Discard states that do not have a seed."));

  cl::opt<bool>
  OnlySeed("only-seed",
           cl::desc("Stop execution after seeding is done without doing regular search."));

  cl::opt<bool>
  AllowSeedExtension("allow-seed-extension",
                     cl::desc("Allow extra (unbound) values to become symbolic during seeding."));

  cl::opt<bool>
  ZeroSeedExtension("zero-seed-extension");

  cl::opt<bool>
  AllowSeedTruncation("allow-seed-truncation",
                      cl::desc("Allow smaller buffers than in seeds."));

  cl::opt<bool>
  NamedSeedMatching("named-seed-matching",
                    cl::desc("Use names to match symbolic objects to inputs."));

  cl::opt<double>
  MaxStaticForkPct("max-static-fork-pct", cl::init(1.));
  cl::opt<double>
  MaxStaticSolvePct("max-static-solve-pct", cl::init(1.));
  cl::opt<double>
  MaxStaticCPForkPct("max-static-cpfork-pct", cl::init(1.));
  cl::opt<double>
  MaxStaticCPSolvePct("max-static-cpsolve-pct", cl::init(1.));

  cl::opt<double>
  MaxInstructionTime("max-instruction-time",
                     cl::desc("Only allow a single instruction to take this much time (default=0 (off))"),
                     cl::init(0));
 
  cl::opt<double>
  SeedTime("seed-time",
           cl::desc("Amount of time to dedicate to seeds, before normal search (default=0 (off))"),
           cl::init(0));
 
  cl::opt<double>
  MaxSTPTime("max-stp-time",
             cl::desc("Maximum amount of time for a single query (default=120s)"),
             cl::init(120.0));
 
  cl::opt<unsigned int>
  StopAfterNInstructions("stop-after-n-instructions",
                         cl::desc("Stop execution after specified number of instructions (0=off)"),
                         cl::init(0));
 
  cl::opt<unsigned>
  MaxForks("max-forks",
           cl::desc("Only fork this many times (-1=off)"),
           cl::init(~0u));
 
  cl::opt<unsigned>
  MaxDepth("max-depth",
           cl::desc("Only allow this many symbolic branches (0=off)"),
           cl::init(0));
 
  cl::opt<unsigned>
  MaxMemory("max-memory",
            cl::desc("Refuse to fork when more above this about of memory (in MB, 0=off)"),
            cl::init(0));

  cl::opt<bool>
  MaxMemoryInhibit("max-memory-inhibit",
            cl::desc("Inhibit forking at memory cap (vs. random terminate)"),
            cl::init(true));

  // use 'external storage' because also needed by tools/klee/main.cpp
  cl::opt<bool, true>
  WriteTracesProxy("write-traces",
           cl::desc("Write .trace file for each terminated state"),
           cl::location(WriteTraces),
           cl::init(false));

  cl::opt<bool>
  UseForkedSTP("use-forked-stp",
                 cl::desc("Run STP in forked process"));

  cl::opt<sockaddr_in_opt>
  STPServer("stp-server",
                 cl::value_desc("host:port"));

  cl::opt<bool>
  ReplayPathOnly("replay-path-only",
            cl::desc("On replay, terminate states when branch decisions have been exhausted"),
            cl::init(true));

  cl::opt<bool>
  ReplayInhibitedForks("replay-inhibited-forks",
            cl::desc("When forking is inhibited, replay the inhibited path as a new state"));

  cl::opt<bool>
  AllExternalWarnings("all-external-warnings");
}


namespace klee {
  RNG theRNG;
}

bool llvm::cl::parser<sockaddr_in_opt>::parse(llvm::cl::Option &O,
     const char *ArgName, const std::string &Arg, sockaddr_in_opt &Val)
{
  // find the separator
  std::string::size_type p = Arg.rfind(':');
  if (p == std::string::npos)
    return O.error("'" + Arg + "' not in format <host>:<port>");

  // read the port number
  unsigned short port;
  if (std::sscanf(Arg.c_str() + p + 1, "%hu", &port) < 1)
    return O.error("'" + Arg.substr(p + 1) + "' invalid port number");

  // resolve server name
  std::string host = Arg.substr(0, p);
  struct hostent* h = gethostbyname(host.c_str());
  if (!h)
    return O.error("cannot resolve '" + host + "' (" + hstrerror(h_errno) + ")");

  // prepare the return value
  Val.str = Arg;
  std::memset(&Val.sin, 0, sizeof(Val.sin));
  Val.sin.sin_family = AF_INET;
  Val.sin.sin_port = htons(port);
  Val.sin.sin_addr = *(struct in_addr*)h->h_addr;

  return false;
}

Solver *constructSolverChain(STPSolver *stpSolver,
                             std::string queryLogPath,
                             std::string stpQueryLogPath,
                             std::string queryPCLogPath,
                             std::string stpQueryPCLogPath)
{
  Solver *solver = stpSolver;

  if (UseSTPQueryPCLog)
    solver = createPCLoggingSolver(solver, stpQueryPCLogPath);

  if (UseFastCexSolver) solver = createFastCexSolver(solver);
  if (UseCexCache) solver = createCexCachingSolver(solver);
  if (UseCache) solver = createCachingSolver(solver);

  if (UseIndependentSolver) solver = createIndependentSolver(solver);

  if (DebugValidateSolver)
    solver = createValidatingSolver(solver, stpSolver);

  if (UseQueryPCLog) solver = createPCLoggingSolver(solver, queryPCLogPath);

  klee_message("BEGIN solver description");
  solver->printName();
  klee_message("END solver description");
 
  return solver;
}


Executor::Executor(
	const InterpreterOptions &opts,
	InterpreterHandler *ih)
  : Interpreter(opts),
    kmodule(0),
    interpreterHandler(ih),
    target_data(0),
    dbgStopPointFn(0),
    statsTracker(0),
    processTree(0),
    symPathWriter(0),
    replayOut(0),
    replayPaths(0),   
    usingSeeds(0),
    atMemoryLimit(false),
    inhibitForking(false),
    haltExecution(false),
    onlyNonCompact(false),
    initialStateCopy(0),
    ivcEnabled(false),
    lastMemoryLimitOperationInstructions(0),
    stpTimeout(MaxInstructionTime ? std::min(MaxSTPTime,MaxInstructionTime) : MaxSTPTime) {
  STPSolver *stpSolver = new STPSolver(UseForkedSTP, STPServer);
  Solver *solver =
    constructSolverChain(stpSolver,
                         interpreterHandler->getOutputFilename("queries.qlog"),
                         interpreterHandler->getOutputFilename("stp-queries.qlog"),
                         interpreterHandler->getOutputFilename("queries.pc"),
                         interpreterHandler->getOutputFilename("stp-queries.pc"));
 
  this->solver = new TimingSolver(solver, stpSolver);
  stpSolver->setTimeout(stpTimeout);

  memory = new MemoryManager();
  stateManager = new ExeStateManager();
}


Executor::~Executor()
{
  std::for_each(timers.begin(), timers.end(), deleteTimerInfo);
  delete stateManager;
  delete memory;
  if (processTree) delete processTree;
  if (statsTracker) delete statsTracker;
  delete solver;
}

/***/

inline void Executor::splitProcessTree(PTreeNode* n, ExecutionState* a, ExecutionState* b)
{
  assert(!n->data);
  std::pair<PTreeNode*, PTreeNode*> res = processTree->split(n, a, b);

  a->ptreeNode = res.first;
  processTree->update(a->ptreeNode, PTree::WeightCompact, !a->isCompactForm);
//  processTree->update(a->ptreeNode, PTree::WeightRunning, !a->isRunning);
  b->ptreeNode = res.second;
  processTree->update(b->ptreeNode, PTree::WeightCompact, !b->isCompactForm);
//  processTree->update(b->ptreeNode, PTree::WeightRunning, !b->isRunning);
}

inline void Executor::replaceStateImmForked(ExecutionState* os, ExecutionState* ns)
{
  stateManager->replaceStateImmediate(os, ns);
  removePTreeState(os);
}
 
MemoryObject * Executor::addExternalObject(ExecutionState &state,
                                           void *addr, unsigned size,
                                           bool isReadOnly) {
  MemoryObject *mo = memory->allocateFixed((uint64_t) (unsigned long) addr,
                                           size, 0, &state);
  ObjectState *os = bindObjectInState(state, mo, false);
  for(unsigned i = 0; i < size; i++) {
    //os->write8(i, ((uint8_t*)addr)[i]);
    state.write8(os, i, ((uint8_t*)addr)[i]);
  }

  if (isReadOnly) os->setReadOnly(true); 
  return mo;
}


bool Executor::isForkingCondition(ExecutionState& current, ref<Expr> condition)
{
  SeedMapType::iterator it = seedMap.find(&current);
  bool isSeeding = it != seedMap.end();

  if (isSeeding) return false;
  if (isa<ConstantExpr>(condition)) return false;
  if (!(MaxStaticForkPct!=1. || MaxStaticSolvePct != 1. ||
      MaxStaticCPForkPct!=1. || MaxStaticCPSolvePct != 1.)) return false;
  if (statsTracker->elapsed() > 60.) return false;
  return true;
}

/* TODO: understand this */
bool Executor::isForkingCallPath(CallPathNode* cpn)
{
  StatisticManager &sm = *theStatisticManager;
  if (MaxStaticForkPct<1. &&
    sm.getIndexedValue(stats::forks, sm.getIndex()) > stats::forks*MaxStaticForkPct)
  {
    return true;
  }

  if (MaxStaticSolvePct<1 &&
    sm.getIndexedValue(stats::solverTime, sm.getIndex()) > stats::solverTime*MaxStaticSolvePct)
  {
    return true;
  }

  /* next conditions require cpn anyway.. */
  if (!cpn) return false;

  if (MaxStaticCPForkPct<1. &&
    (cpn->statistics.getValue(stats::forks) > stats::forks*MaxStaticCPForkPct))
    return true;

  if (MaxStaticCPForkPct<1. &&
    (cpn->statistics.getValue(stats::solverTime) >
      stats::solverTime*MaxStaticCPSolvePct))
    return true;

  return false;
}

Executor::StatePair
Executor::fork(ExecutionState &current, ref<Expr> condition, bool isInternal)
{
  ref<Expr> conditions[2];

  // !!! is this the correct behavior?
  if (isForkingCondition(current, condition)) {
    CallPathNode *cpn = current.stack.back().callPathNode;
    if (isForkingCallPath(cpn)) {
      ref<ConstantExpr> value;
      bool success = solver->getValue(current, condition, value);
      assert(success && "FIXME: Unhandled solver failure");
      addConstraint(current, EqExpr::create(value, condition));
      condition = value;
    }
  }

/* XXX what is condition[0], then!? - AJR */
//  conditions[0] = Expr::createIsZero(condition);
  conditions[1] = condition;

  StateVector results = fork(current, 2, conditions, isInternal, true);
  return std::make_pair(results[1], results[0]);
}

bool Executor::forkSetupNoSeeding(
  ExecutionState& current,
  unsigned N, std::vector<bool>& res,
  bool isInternal,
  unsigned& validTargets, bool& forkCompact, bool& wasReplayed)
{
  if (!isInternal && current.isCompactForm) {
    // Can't fork compact states; sanity check
    assert(false && "invalid state");
  } else if (!isInternal && ReplayPathOnly && current.isReplay
    && current.replayBranchIterator == current.branchDecisionsSequence.end())
  {
    // Done replaying this state, so kill it (if -replay-path-only)
    terminateStateEarly(current, "replay path exhausted");
    return false;
  } else if (!isInternal
    && current.replayBranchIterator != current.branchDecisionsSequence.end())
  {
    // Replaying non-internal fork; read value from replayBranchIterator
#ifdef INCLUDE_INSTR_ID_IN_PATH_INFO
    assert(current.prevPC->info->assemblyLine
      == (*current.replayBranchIterator).second &&
      "branch instruction IDs do not match");
#endif
    unsigned targetIndex = (*current.replayBranchIterator).first;
    ++current.replayBranchIterator;
    wasReplayed = true;
    // Verify that replay target matches current path constraints
    assert(targetIndex <= N && "replay target out of range");
    if (!res[targetIndex]) {
      std::stringstream ss;
      ss << "hit invalid branch in replay path mode (line="
         << current.prevPC->info->assemblyLine << ", expected target="
         << targetIndex << ", actual targets=";
      bool first = true;
      for(unsigned i = 0; i < N; i++) {
        if (!res[i]) continue;
        if (!first) ss << ",";
        ss << i;
        first = false;
      }
      ss << ")";
      terminateStateOnError(current, ss.str().c_str(), "branch.err");
      klee_warning("hit invalid branch in replay path mode");
      return false;
    }
    else {
      // Suppress forking; constraint will be added to path below
      res.assign(N, false);
      res[targetIndex] = true;
    }
  } // if (!isInternal && current.replayBranchIterator != end)
  else if (validTargets > 1) {
    // Multiple branch directions are possible; check for flags that
    // prevent us from forking here
    assert(!replayOut && "in replay mode, only one branch can be true.");

    if (isInternal) return true;

    const char* reason = 0;
    if (MaxMemoryInhibit && atMemoryLimit) reason = "memory cap exceeded";
    if (current.forkDisabled) reason = "fork disabled on current path";
    if (inhibitForking) reason = "fork disabled globally";
    if (MaxForks!=~0u && stats::forks >= MaxForks) reason = "max-forks reached";

    // Skipping this fork for one of the above reasons; randomly pick target
    if (!reason) return true;
    if (ReplayInhibitedForks) {
      klee_warning_once(reason, "forking into compact forms (%s)", reason);
      forkCompact = true;
      return true;
    }

    klee_warning_once(reason, "skipping fork and pruning randomly (%s)", reason);
    TimerStatIncrementer timer(stats::forkTime);
    unsigned randIndex = (theRNG.getInt32() % validTargets) + 1;
    unsigned condIndex;
    for(condIndex = 0; condIndex < N; condIndex++) {
      if (res[condIndex]) randIndex--;
      if (!randIndex) break;
    }
    assert(condIndex < N);
    validTargets = 1;
    res.assign(N, false);
    res[condIndex] = true;
  } // if (validTargets > 1)
  return true;
}


void Executor::forkSetupSeeding(
  ExecutionState& current,
  unsigned N, ref<Expr> conditions[],
  std::vector<bool>& res,
  std::vector<std::list<SeedInfo> >& resSeeds,
  unsigned& validTargets)
{
  SeedMapType::iterator it = seedMap.find(&current);
  assert (it != seedMap.end());

  // Fix branch in only-replay-seed mode, if we don't have both true
  // and false seeds.

  // Assume each seed only satisfies one condition (necessarily true
  // when conditions are mutually exclusive and their conjunction is
  // a tautology).
  // This partitions the seed set for the current state
  for (std::vector<SeedInfo>::iterator siit = it->second.begin(),
         siie = it->second.end(); siit != siie; ++siit) {
    unsigned i;
    for (i = 0; i < N; ++i) {
      ref<ConstantExpr> seedCondRes;
      bool success = solver->getValue(current,
        siit->assignment.evaluate(conditions[i]), seedCondRes);
      assert(success && "FIXME: Unhandled solver failure");
      if (seedCondRes->isTrue()) break;
    }

    // If we didn't find a satisfying condition, randomly pick one
    // (the seed will be patched).
    if (i == N) i = theRNG.getInt32() % N;

    resSeeds[i].push_back(*siit);
  }

  // Clear any valid conditions that seeding rejects
  if ((current.forkDisabled || OnlyReplaySeeds) && validTargets > 1) {
    validTargets = 0;
    for (unsigned i = 0; i < N; i++) {
      if (resSeeds[i].empty()) res[i] = false;
      if (res[i]) validTargets++;
    }
    assert(validTargets && "seed must result in at least one valid target");
  }

  // Remove seeds corresponding to current state
  seedMap.erase(it);
 
  // !!! it's possible for the current state to end up with no seeds. Does
  // this matter? Old fork() used to handle it but branch() didn't.
}

// !!! for normal branch, conditions = {false,true} so that replay 0,1 reflects
// index
Executor::StateVector
Executor::fork(ExecutionState &current,
               unsigned N, ref<Expr> conditions[],
               bool isInternal, bool isBranch) {
  std::vector<bool> res(N, false);
  StateVector resStates(N, NULL);
  std::vector<std::list<SeedInfo> > resSeeds(N);

  // Evaluate fork conditions
  double timeout = stpTimeout;
  unsigned condIndex, validTargets = 0, feasibleTargets = 0;

  SeedMapType::iterator it = seedMap.find(&current);
  bool isSeeding = it != seedMap.end();
  if (isSeeding) timeout *= it->second.size();

  if (isBranch) {
    Solver::Validity result;
    solver->setTimeout(timeout);
    bool success = solver->evaluate(current, conditions[1], result);
    solver->setTimeout(stpTimeout);
    if (!success) {
      terminateStateEarly(current, "query timed out");
      return StateVector(N, NULL);
    }
    res[0] = (result == Solver::False || result == Solver::Unknown);
    res[1] = (result == Solver::True || result == Solver::Unknown);
    validTargets = (result == Solver::Unknown ? 2 : 1);
    if (validTargets > 1 || isSeeding)
      conditions[0] = Expr::createIsZero(conditions[1]);
  } else {
    for(condIndex = 0; condIndex < N; condIndex++) {
      ConstantExpr *CE = dyn_cast<ConstantExpr>(conditions[condIndex]);
      bool result;
      // If condition is a constant (e.g., from constant switch statement), don't
      // generate a query
      if (CE) {
        if (CE->isFalse()) result = false;
        else if (CE->isTrue()) result = true;
        else assert(false && "Invalid constant fork condition");
      }
      else {
        solver->setTimeout(timeout);
        bool success = solver->mayBeTrue(current, conditions[condIndex], result);
        solver->setTimeout(stpTimeout);
        if (!success) {
          terminateStateEarly(current, "query timed out");
          return StateVector(N, NULL);
        }
      }
      res[condIndex] = result;
      if (result)
        validTargets++;
    }
  }
  // need a copy telling us whether or not we need to add constraints later;
  // especially important if we skip a fork for whatever reason
  feasibleTargets = validTargets;

  assert(validTargets && "invalid set of fork conditions");

  bool forkCompact = false;
  bool wasReplayed = false;
  if (!isSeeding) {
    if (!forkSetupNoSeeding(current, N, res, isInternal,
      validTargets, forkCompact, wasReplayed))
      return StateVector(N, NULL);
  } else {
    forkSetupSeeding(current, N, conditions, res, resSeeds, validTargets);
  }

  bool curStateUsed = false;
  // Loop for actually forking states
  for(condIndex = 0; condIndex < N; condIndex++) {
    ExecutionState *baseState = &current;
    // Process each valid target and generate a state
    if (!res[condIndex]) continue;

    ExecutionState *newState;
    if (!curStateUsed) {
      resStates[condIndex] = baseState;
      curStateUsed = true;
    } else {
      assert(!forkCompact || ReplayInhibitedForks);

      // Update stats
      TimerStatIncrementer timer(stats::forkTime);
      ++stats::forks;
     
      // Do actual state forking
      newState = forkCompact ? current.branchForReplay()
                             : current.branch();
      stateManager->add(newState);
      resStates[condIndex] = newState;

      // Split pathWriter stream
      if (!isInternal) {
        if (symPathWriter && newState != &current)
          newState->symPathOS = symPathWriter->open(current.symPathOS);
      }

      // Randomize path tree layout
      if (RandomizeFork && theRNG.getBool()) {
        std::swap(baseState, newState);
      }

      // Update path tree with new states
      current.ptreeNode->data = 0;
      splitProcessTree(current.ptreeNode, baseState, newState);
    }
  } // for

  // Loop for bookkeeping (loops must be separate since states are forked from
  // each other)
  for(condIndex = 0; condIndex < N; condIndex++) {
    if (!res[condIndex]) continue;

    ExecutionState *curState = resStates[condIndex];
    assert(curState);

    // Add path constraint
    if (!curState->isCompactForm && feasibleTargets > 1)
      addConstraint(*curState, conditions[condIndex]);
    // XXX - even if the constraint is provable one way or the other we
    // can probably benefit by adding this constraint and allowing it to
    // reduce the other constraints. For example, if we do a binary
    // search on a particular value, and then see a comparison against
    // the value it has been fixed at, we should take this as a nice
    // hint to just use the single constraint instead of all the binary
    // search ones. If that makes sense.

    // Kinda gross, do we even really still want this option?
    if (MaxDepth && MaxDepth<=curState->depth) {
      terminateStateEarly(*curState, "max-depth exceeded");
      resStates[condIndex] = NULL;
      continue;
    }

    // Auxiliary bookkeeping
    if (!isInternal) {
      if (symPathWriter && validTargets > 1) {
        std::stringstream ssPath;
        ssPath << condIndex << "\n";
        curState->symPathOS << ssPath.str();
      }

      // only track NON-internal branches
      if (!wasReplayed
          && curState->replayBranchIterator ==
          curState->branchDecisionsSequence.end())
      {
        curState->branchDecisionsSequence.push_back(
          condIndex,
          current.prevPC->info->assemblyLine);
        curState->replayBranchIterator =
          curState->branchDecisionsSequence.end();
      }
    } // if (!isInternal)

    if (isSeeding) {
      seedMap[curState].insert(seedMap[curState].end(),
        resSeeds[condIndex].begin(), resSeeds[condIndex].end());
    }
  } // for

  return resStates;
}

void Executor::addConstraint(ExecutionState &state, ref<Expr> condition) {
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(condition)) {
    assert(CE->isTrue() && "attempt to add invalid constraint");
    return;
  }

  // Check to see if this constraint violates seeds.
  std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it =
    seedMap.find(&state);
  if (it != seedMap.end()) {
    bool warn = false;
    for (std::vector<SeedInfo>::iterator siit = it->second.begin(),
           siie = it->second.end(); siit != siie; ++siit) {
      bool res;
      bool success =
        solver->mustBeFalse(state, siit->assignment.evaluate(condition), res);
      assert(success && "FIXME: Unhandled solver failure");
      (void) success;
      if (res) {
        siit->patchSeed(state, condition, solver);
        warn = true;
      }
    }
    if (warn)
      klee_warning("seeds patched for violating constraint");
  }

  state.addConstraint(condition);
  if (ivcEnabled)
    doImpliedValueConcretization(state, condition,
                                 ConstantExpr::alloc(1, Expr::Bool));
}

ref<klee::ConstantExpr> Executor::evalConstant(Constant *c)
{
  if (llvm::ConstantExpr *ce = dyn_cast<llvm::ConstantExpr>(c)) {
    return evalConstantExpr(ce);
  } else if (const ConstantInt *ci = dyn_cast<ConstantInt>(c)) {
    return ConstantExpr::alloc(ci->getValue());
  } else if (const ConstantFP *cf = dyn_cast<ConstantFP>(c)) {     
    return ConstantExpr::alloc(cf->getValueAPF().bitcastToAPInt());
  } else if (const GlobalValue *gv = dyn_cast<GlobalValue>(c)) {
    globaladdr_map::iterator it = globalAddresses.find(gv);

    if (it == globalAddresses.end()) {
    	const Function	*f;
    	f = dynamic_cast<const Function*>(gv);
	if (f && f->isDeclaration()) {
		/* stupid stuff to get vexllvm imported functions working */
		Function	*f2; 
		f2 = kmodule->module->getFunction(f->getNameStr());
		it = globalAddresses.find(f2);
	}
    }
    assert (it != globalAddresses.end() && "No global address!");
    return it->second;
  } else if (isa<ConstantPointerNull>(c)) {
    return Expr::createPointer(0);
  } else if (isa<UndefValue>(c) || isa<ConstantAggregateZero>(c)) {
    return ConstantExpr::create(0, getWidthForLLVMType(c->getType()));
  } else if (isa<ConstantVector>(c)) {
    return ConstantExpr::createVector(cast<ConstantVector>(c));
  } else {
    // Constant{AggregateZero,Array,Struct,Vector}
    fprintf(stderr, "AIEEEEEEEE!\n");
    c->dump();
    assert(0 && "invalid argument to evalConstant()");
  }
}

void Executor::bindLocal(KInstruction *target, ExecutionState &state,
                         ref<Expr> value) {
  //getDestCell(state, target).value = value;
    state.writeLocalCell(state.stack.size() - 1, target->dest, value);
}

void Executor::bindArgument(KFunction *kf, unsigned index,
                            ExecutionState &state, ref<Expr> value) {
  ///getArgumentCell(state, kf, index).value = value;
    state.writeLocalCell(state.stack.size() - 1, kf->getArgRegister(index), value);
 
}

ref<Expr> Executor::toUnique(const ExecutionState &state,
                             ref<Expr> &e) {
  ref<Expr> result = e;

  if (!isa<ConstantExpr>(e)) {
    ref<ConstantExpr> value;
    bool isTrue = false;

    if (solver->getValue(state, e, value) &&
        solver->mustBeTrue(state, EqExpr::create(e, value), isTrue) &&
        isTrue)
      result = value;
  }
 
  return result;
}

/* Concretize the given expression, and return a possible constant value.
   'reason' is just a documentation string stating the reason for concretization. */
ref<klee::ConstantExpr>
Executor::toConstant(ExecutionState &state,
                     ref<Expr> e,
                     const char *reason) {
  e = state.constraints.simplifyExpr(e);
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(e))
    return CE;

  ref<ConstantExpr> value;
  bool success = solver->getValue(state, e, value);
  assert(success && "FIXME: Unhandled solver failure");
  (void) success;
   
  std::ostringstream os;
  os << "silently concretizing (reason: " << reason << ") expression " << e
     << " to value " << value
     << " (" << (*(state.pc)).info->file << ":" << (*(state.pc)).info->line << ")";
     
  if (AllExternalWarnings)
    klee_warning(reason, os.str().c_str());
  else
    klee_warning_once(reason, "%s", os.str().c_str());

  addConstraint(state, EqExpr::create(e, value));
   
  return value;
}

bool Executor::getSeedInfoIterRange(
  ExecutionState* s, SeedInfoIterator &b, SeedInfoIterator& e)
{
  std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it;
  it = seedMap.find(s);
  if (it == seedMap.end()) return false;
  b = it->second.begin();
  e = it->second.end();
  return false;
}

void Executor::executeGetValue(ExecutionState &state,
                               ref<Expr> e,
                               KInstruction *target) {
  bool              isSeeding;
  SeedInfoIterator  si_begin, si_end;

  e = state.constraints.simplifyExpr(e);
  isSeeding = getSeedInfoIterRange(&state, si_begin, si_end);

  if (!isSeeding || isa<ConstantExpr>(e)) {
    ref<ConstantExpr> value;
    bool success = solver->getValue(state, e, value);
    assert(success && "FIXME: Unhandled solver failure");
    (void) success;
    bindLocal(target, state, value);
    return;
  }

  std::set< ref<Expr> > values;
  for (SeedInfoIterator siit = si_begin, siie = si_end; siit != siie; ++siit) {
    ref<ConstantExpr> value;
    bool success =
      solver->getValue(state, siit->assignment.evaluate(e), value);
    assert(success && "FIXME: Unhandled solver failure");
    (void) success;
    values.insert(value);
  }
 
  std::vector< ref<Expr> > conditions;
  for (std::set< ref<Expr> >::iterator vit = values.begin(),
         vie = values.end(); vit != vie; ++vit)
    conditions.push_back(EqExpr::create(e, *vit));

  StateVector branches;
  branches = fork(state, conditions.size(), conditions.data(), true);
 
  StateVector::iterator bit = branches.begin();
  for (std::set< ref<Expr> >::iterator vit = values.begin(),
         vie = values.end(); vit != vie; ++vit) {
    ExecutionState *es = *bit;
    if (es) bindLocal(target, *es, *vit);
    ++bit;
  }
}

void Executor::stepInstruction(ExecutionState &state)
{
  if (DebugPrintInstructions) {
    printFileLine(state, state.pc);
    std::cerr << std::setw(10) << stats::instructions << " "
              << *(state.pc->inst) << "\n";
  }

  if (statsTracker) {
    statsTracker->stepInstruction(state);
  }

  ++stats::instructions;
  state.prevPC = state.pc;
  ++state.pc;
  if (stats::instructions==StopAfterNInstructions)
    haltExecution = true;
}

void Executor::executeCallNonDecl(
	ExecutionState &state, 
	KInstruction *ki,
	Function *f,
	std::vector< ref<Expr> > &arguments)
{
	// FIXME: I'm not really happy about this reliance on prevPC but it is ok, I
	// guess. This just done to avoid having to pass KInstIterator everywhere
	// instead of the actual instruction, since we can't make a KInstIterator
	// from just an instruction (unlike LLVM).
	KFunction	*kf;
	unsigned	callingArgs, funcArgs, numFormals;

	assert (!f->isDeclaration() && "Expects a non-declaration function!");
	kf = kmodule->getKFunction(f);
	assert (kf != NULL && "Executing non-shadowed function");

	state.pushFrame(state.prevPC, kf);
	state.pc = kf->instructions;

	if (statsTracker)
		statsTracker->framePushed(
			state,
			&state.stack[state.stack.size()-2]);

	// TODO: support "byval" parameter attribute
	// TODO: support zeroext, signext, sret attributes
	//
	//

	callingArgs = arguments.size();
	funcArgs = f->arg_size();
	if (callingArgs < funcArgs) {
		terminateStateOnError(
			state,
			"calling function with too few arguments",
			"user.err");
		return;
	}

	if (!f->isVarArg()) {
		if (callingArgs > funcArgs) {
			klee_warning_once(f, "calling %s with extra arguments.", 
			f->getName().data());
		}
	} else {
		if (!setupCallVarArgs(state, funcArgs, arguments))
			return;
	}

	numFormals = f->arg_size();
	for (unsigned i=0; i<numFormals; ++i) 
		bindArgument(kf, i, state, arguments[i]);
}


bool Executor::setupCallVarArgs(
	ExecutionState& state,
	unsigned funcArgs,
	std::vector<ref<Expr> >& arguments)
{
	MemoryObject	*mo;
	ObjectState	*os;
	unsigned	size, offset, callingArgs;

	StackFrame &sf = state.stack.back();

	callingArgs = arguments.size();
	size = 0;
	for (unsigned i = funcArgs; i < callingArgs; i++) {
	// FIXME: This is really specific to the architecture, not the pointer
	// size. This happens to work fir x86-32 and x86-64, however.
		Expr::Width WordSize = Context::get().getPointerWidth();
		if (WordSize == Expr::Int32) {
			size += Expr::getMinBytesForWidth(
				arguments[i]->getWidth());
		} else {
			size += llvm::RoundUpToAlignment(
				arguments[i]->getWidth(), WordSize)/8;
		}
	}

	mo = memory->allocate(size, true, false, state.prevPC->inst, &state);
	sf.varargs = mo;
	if (!mo) {
		terminateStateOnExecError(state, "out of memory (varargs)");
		return false;
	}

	os = bindObjectInState(state, mo, true);
	offset = 0;
	for (unsigned i = funcArgs; i < callingArgs; i++) {
	// FIXME: This is really specific to the architecture, not the pointer
	// size. This happens to work fir x86-32 and x86-64, however.
		Expr::Width WordSize = Context::get().getPointerWidth();
		if (WordSize == Expr::Int32) {
			//os->write(offset, arguments[i]);
			state.write(os, offset, arguments[i]);
			offset += Expr::getMinBytesForWidth(
				arguments[i]->getWidth());
		} else {
			assert (WordSize==Expr::Int64 && "Unknown word size!");

			//os->write(offset, arguments[i]);
			state.write(os, offset, arguments[i]);
			offset += llvm::RoundUpToAlignment(
					arguments[i]->getWidth(), 
					WordSize) / 8;
		}
	}

	return true;
}


void Executor::executeCall(ExecutionState &state,
                           KInstruction *ki,
                           Function *f,
                           std::vector< ref<Expr> > &arguments)
{
  assert (f);

  if (WriteTraces)
    state.exeTraceMgr.addEvent(
      new FunctionCallTraceEvent(state, ki, f->getName()));

  Instruction *i = ki->inst;

  if (!f->isDeclaration() || kmodule->module->getFunction(f->getNameStr())) {
    /* this is so that vexllvm linked modules work */
    Function *f2 = kmodule->module->getFunction(f->getNameStr());
    if (f2 == NULL) f2 = f;
    if (!f2->isDeclaration()) {
	    executeCallNonDecl(state, ki, f2, arguments);
	    return;
    }
  }
 
  switch(f->getIntrinsicID()) {
  case Intrinsic::not_intrinsic:
    // state may be destroyed by this call, cannot touch
    callExternalFunction(state, ki, f, arguments);
    break;
       
    // va_arg is handled by caller and intrinsic lowering, see comment for
    // ExecutionState::varargs
  case Intrinsic::vastart:  {
    StackFrame &sf = state.stack.back();
    assert(sf.varargs &&
           "vastart called in function with no vararg object");
     // FIXME: This is really specific to the architecture, not the pointer
    // size. This happens to work fir x86-32 and x86-64, however.
    Expr::Width WordSize = Context::get().getPointerWidth();
    if (WordSize == Expr::Int32) {
      executeMemoryOperation(state, true, arguments[0],
                             sf.varargs->getBaseExpr(), 0);
    } else {
      assert(WordSize == Expr::Int64 && "Unknown word size!");
       // X86-64 has quite complicated calling convention. However,
      // instead of implementing it, we can do a simple hack: just
      // make a function believe that all varargs are on stack.
      executeMemoryOperation(state, true, arguments[0],
                             ConstantExpr::create(48, 32), 0); // gp_offset
      executeMemoryOperation(state, true,
                             AddExpr::create(arguments[0],
                                             ConstantExpr::create(4, 64)),
                             ConstantExpr::create(304, 32), 0); // fp_offset
      executeMemoryOperation(state, true,
                             AddExpr::create(arguments[0],
                                             ConstantExpr::create(8, 64)),
                             sf.varargs->getBaseExpr(), 0); // overflow_arg_area
      executeMemoryOperation(state, true,
                             AddExpr::create(arguments[0],
                                             ConstantExpr::create(16, 64)),
                             ConstantExpr::create(0, 64), 0); // reg_save_area
    }
    break;
  }
  case Intrinsic::vaend:
    // va_end is a noop for the interpreter.
    //
    // FIXME: We should validate that the target didn't do something bad
    // with vaeend, however (like call it twice).
    break;
     
  case Intrinsic::vacopy:
    // va_copy should have been lowered.
    //
    // FIXME: It would be nice to check for errors in the usage of this as
    // well.
  default:
    klee_error("unknown intrinsic: %s", f->getName().data());
  }
  if (InvokeInst *ii = dyn_cast<InvokeInst>(i))
    transferToBasicBlock(ii->getNormalDest(), i->getParent(), state);
}

void Executor::transferToBasicBlock(
	BasicBlock *dst, BasicBlock *src,
        ExecutionState &state)
{
  // Note that in general phi nodes can reuse phi values from the same
  // block but the incoming value is the eval() result *before* the
  // execution of any phi nodes. this is pathological and doesn't
  // really seem to occur, but just in case we run the PhiCleanerPass
  // which makes sure this cannot happen and so it is safe to just
  // eval things in order. The PhiCleanerPass also makes sure that all
  // incoming blocks have the same order for each PHINode so we only
  // have to compute the index once.
  //
  // With that done we simply set an index in the state so that PHI
  // instructions know which argument to eval, set the pc, and continue.
 
  // XXX this lookup has to go ?
  KFunction *kf = state.stack.back().kf;
  unsigned entry = kf->basicBlockEntry[dst];
  state.pc = &kf->instructions[entry];
  if (state.pc->inst->getOpcode() == Instruction::PHI) {
    PHINode *first = static_cast<PHINode*>(state.pc->inst);
    state.incomingBBIndex = first->getBasicBlockIndex(src);
  }
}

void Executor::printFileLine(ExecutionState &state, KInstruction *ki) {
  const InstructionInfo &ii = *ki->info;
  if (ii.file != "")
    std::cerr << "     " << ii.file << ":" << ii.line << ":";
  else
    std::cerr << "     [no debug info]:";
}

bool Executor::isDebugIntrinsic(const Function *f)
{
  // Fast path, getIntrinsicID is slow.
  if (f == dbgStopPointFn) return true;

  switch (f->getIntrinsicID()) {
  case Intrinsic::dbg_stoppoint:
  case Intrinsic::dbg_region_start:
  case Intrinsic::dbg_region_end:
  case Intrinsic::dbg_func_start:
  case Intrinsic::dbg_declare:
    return true;

  default:
    return false;
  }
}

static inline const llvm::fltSemantics * fpWidthToSemantics(unsigned width) {
  switch(width) {
  case Expr::Int32:
    return &llvm::APFloat::IEEEsingle;
  case Expr::Int64:
    return &llvm::APFloat::IEEEdouble;
  case Expr::Fl80:
    return &llvm::APFloat::x87DoubleExtended;
  default:
    return 0;
  }
}

void Executor::instRetFromNested(ExecutionState &state, KInstruction *ki)
{
  ReturnInst *ri = cast<ReturnInst>(ki->inst);
  KInstIterator kcaller = state.getCaller();
  Instruction *caller = kcaller ? kcaller->inst : 0;
  bool isVoidReturn = (ri->getNumOperands() == 0);
  ref<Expr> result = ConstantExpr::alloc(0, Expr::Bool);

  assert (state.stack.size() > 1);

  if (!isVoidReturn) result = eval(ki, 0, state).value;
 
  state.popFrame();

  if (statsTracker) statsTracker->framePopped(state);

  if (InvokeInst *ii = dyn_cast<InvokeInst>(caller)) {
    transferToBasicBlock(ii->getNormalDest(), caller->getParent(), state);
  } else {
    state.pc = kcaller;
    ++state.pc;
  }

  if (isVoidReturn) {
    // We check that the return value has no users instead of
    // checking the type, since C defaults to returning int for
    // undeclared functions.
    if (!caller->use_empty()) {
      terminateStateOnExecError(state, "return void when caller expected a result");
    }
    return;
  }

  assert (!isVoidReturn);
  const Type *t = caller->getType();
  if (t == Type::getVoidTy(getGlobalContext())) return;

  // may need to do coercion due to bitcasts
  Expr::Width from = result->getWidth();
  Expr::Width to = getWidthForLLVMType(t);
   
  if (from != to) {
    CallSite cs = (isa<InvokeInst>(caller) ? CallSite(cast<InvokeInst>(caller)) :
                   CallSite(cast<CallInst>(caller)));

    // XXX need to check other param attrs ?
    if (cs.paramHasAttr(0, llvm::Attribute::SExt)) {
      result = SExtExpr::create(result, to);
    } else {
      result = ZExtExpr::create(result, to);
    }
  }
  bindLocal(kcaller, state, result);

}

void Executor::instRet(ExecutionState &state, KInstruction *ki)
{
  if (state.stack.size() <= 1) {
    assert (	!(state.getCaller()) && 
    		"caller set on initial stack frame");
    terminateStateOnExit(state);
    return;
  }

  instRetFromNested(state, ki);
}

void Executor::instBranch(ExecutionState& state, KInstruction* ki)
{
  BranchInst *bi = cast<BranchInst>(ki->inst);
  if (bi->isUnconditional()) {
    transferToBasicBlock(bi->getSuccessor(0), bi->getParent(), state);
    return;
  }

  // FIXME: Find a way that we don't have this hidden dependency.
  assert(bi->getCondition() == bi->getOperand(0) &&
         "Wrong operand index!");
  const Cell &cond = eval(ki, 0, state);
  StatePair branches = fork(state, cond.value, false);

  if (WriteTraces) {
    bool isTwoWay = (branches.first && branches.second);

    if (branches.first) {
      branches.first->exeTraceMgr.addEvent(
        new BranchTraceEvent(state, ki, true, isTwoWay));
    }

    if (branches.second) {
      branches.second->exeTraceMgr.addEvent(
        new BranchTraceEvent(state, ki, false, isTwoWay));
    }
  }

  KFunction *kf = state.stack.back().kf;
//      ExecutorThread *thread = ExecutorThread::getThread();

  // NOTE: There is a hidden dependency here, markBranchVisited
  // requires that we still be in the context of the branch
  // instruction (it reuses its statistic id). Should be cleaned
  // up with convenient instruction specific data.
  if (statsTracker && state.stack.back().kf->trackCoverage)
    statsTracker->markBranchVisited(branches.first, branches.second);

  /* FIXME: Refactor */
  if (branches.first) {
    // reconstitute the state if it was forked into compact form but will
    // immediately cover a new instruction
    // !!! this can be done more efficiently by simply forking a regular
    // state inside fork() but that will change the fork() semantics
    if (branches.first->isCompactForm && kf->trackCoverage &&
        theStatisticManager->getIndexedValue(stats::uncoveredInstructions,
        kf->instructions[kf->basicBlockEntry[bi->getSuccessor(0)]]->info->
        id)) {
      ExecutionState *newState = branches.first->
        reconstitute(*initialStateCopy);
      replaceStateImmForked(branches.first, newState);
      branches.first = newState;
    }
    if (!branches.first->isCompactForm)
      transferToBasicBlock(bi->getSuccessor(0), bi->getParent(),
        *branches.first);
  }

  if (branches.second) {
    // reconstitute the state if it was forked into compact form but will
    // immediately cover a new instruction
    // !!! this can be done more efficiently by simply forking a regular
    // state inside fork() but that will change the fork() semantics
    if (branches.second->isCompactForm && kf->trackCoverage &&
        theStatisticManager->getIndexedValue(stats::uncoveredInstructions,
        kf->instructions[kf->basicBlockEntry[bi->getSuccessor(1)]]->info->
        id)) {
      ExecutionState *newState = branches.second->
        reconstitute(*initialStateCopy);
      replaceStateImmForked(branches.second, newState);
      branches.second = newState;
    }
    if (!branches.second->isCompactForm)
      transferToBasicBlock(bi->getSuccessor(1), bi->getParent(),
        *branches.second);
  }
}

void Executor::instCall(ExecutionState& state, KInstruction *ki)
{
  CallSite cs(ki->inst);
  unsigned numArgs = cs.arg_size();
  Function *f = getCalledFunction(cs, state);

  // Skip debug intrinsics, we can't evaluate their metadata arguments.
  if (f && isDebugIntrinsic(f)) return;

  // evaluate arguments
  std::vector< ref<Expr> > arguments;
  arguments.reserve(numArgs);

  for (unsigned j=0; j<numArgs; ++j)
    arguments.push_back(eval(ki, j+1, state).value);

  if (!f) {
    // special case the call with a bitcast case
    Value *fp = cs.getCalledValue();
    llvm::ConstantExpr *ce = dyn_cast<llvm::ConstantExpr>(fp);

    if (isa<InlineAsm>(fp)) {
      terminateStateOnExecError(state, "inline assembly is unsupported");
      return;
    }
    
    if (ce && ce->getOpcode()==Instruction::BitCast) {
	f = dyn_cast<Function>(ce->getOperand(0));
	executeBitCast(state, cs, ce, arguments);
    }
  }

  if (f) {
    executeCall(state, ki, f, arguments);
  } else {
    executeSymbolicFuncPtr(state, ki, arguments);
  }
}

void Executor::executeBitCast(
	ExecutionState &state, 
	CallSite&		cs,
	llvm::ConstantExpr*	ce,
	std::vector< ref<Expr> > &arguments)
{
	llvm::Function		*f;
	const FunctionType	*fType, *ceType;

	f = dyn_cast<Function>(ce->getOperand(0));
     	assert(f && "XXX unrecognized constant expression in call");

        fType = dyn_cast<FunctionType>(
		cast<PointerType>(f->getType())->getElementType());
	ceType = dyn_cast<FunctionType>(
		cast<PointerType>(ce->getType())->getElementType());

	assert(fType && ceType && "unable to get function type");

	// XXX check result coercion

	// XXX this really needs thought and validation
	unsigned i=0;
	for (	std::vector< ref<Expr> >::iterator
		ai = arguments.begin(), ie = arguments.end();
		ai != ie; ++ai, i++)
	{
		Expr::Width to, from;

		if (i >= fType->getNumParams()) continue;

		from = (*ai)->getWidth();
		to = getWidthForLLVMType(fType->getParamType(i));
		if (from == to) continue;

		// XXX need to check other param attrs ?
		if (cs.paramHasAttr(i+1, llvm::Attribute::SExt)) {
			arguments[i] = SExtExpr::create(arguments[i], to);
		} else {
			arguments[i] = ZExtExpr::create(arguments[i], to);
		}
	}
}

void Executor::executeSymbolicFuncPtr(
	ExecutionState &state,
        KInstruction *ki,
        std::vector< ref<Expr> > &arguments)
{
    ref<Expr> v = eval(ki, 0, state).value;

    ExecutionState *free = &state;
    bool hasInvalid = false, first = true;

    /* XXX This is wasteful, no need to do a full evaluate since we
       have already got a value. But in the end the caches should
       handle it for us, albeit with some overhead. */
    do {
      ref<ConstantExpr> value;
      bool success = solver->getValue(*free, v, value);
      assert(success && "FIXME: Unhandled solver failure");
      (void) success;
      StatePair res = fork(*free, EqExpr::create(v, value), true);
      if (res.first) {
        uint64_t addr = value->getZExtValue();
        if (legalFunctions.count(addr)) {
          Function* f = (Function*) addr;

         // Don't give warning on unique resolution
          if (res.second || !first)
            klee_warning_once((void*) (unsigned long) addr,
                              "resolved symbolic function pointer to: %s",
                              f->getName().data());

          executeCall(*res.first, ki, f, arguments);
        } else {
          if (!hasInvalid) {
            terminateStateOnExecError(state, "invalid function pointer");
            hasInvalid = true;
          }
        }
      }

      first = false;
      free = res.second;
    } while (free);
}

void Executor::instCmp(ExecutionState& state, KInstruction *ki)
{
  CmpInst *ci = cast<CmpInst>(ki->inst);
  ICmpInst *ii = cast<ICmpInst>(ci);
  ICmpInst::Predicate pred;

  ref<Expr> left = eval(ki, 0, state).value;
  ref<Expr> right = eval(ki, 1, state).value;
  ref<Expr> result;

  pred = ii->getPredicate();
  switch(pred) {
  case ICmpInst::ICMP_EQ: result = EqExpr::create(left, right); break;
  case ICmpInst::ICMP_NE: result = NeExpr::create(left, right); break;
  case ICmpInst::ICMP_UGT: result = UgtExpr::create(left, right); break;
  case ICmpInst::ICMP_UGE: result = UgeExpr::create(left, right); break;
  case ICmpInst::ICMP_ULT: result = UltExpr::create(left, right); break;
  case ICmpInst::ICMP_ULE: result = UleExpr::create(left, right); break;
  case ICmpInst::ICMP_SGT: result = SgtExpr::create(left, right); break;
  case ICmpInst::ICMP_SGE: result = SgeExpr::create(left, right); break;
  case ICmpInst::ICMP_SLT: result = SltExpr::create(left, right); break;
  case ICmpInst::ICMP_SLE: result = SleExpr::create(left, right); break;
  default:
    terminateStateOnExecError(state, "invalid ICmp predicate");
    return;
  }


  bindLocal(ki, state, result);
//  klee_message("Updated to: %s\n", states2str(states).c_str());
}

void Executor::instSwitch(ExecutionState& state, KInstruction *ki)
{
  SwitchInst *si = cast<SwitchInst>(ki->inst);
  ref<Expr> cond = eval(ki, 0, state).value;
  unsigned cases = si->getNumCases();
  BasicBlock *bb = si->getParent();

  std::map<BasicBlock*, ref<Expr> > targets;
  std::vector<ref<Expr> > caseConds;
  std::vector<BasicBlock*> caseDests;
  StateVector resultStates;

  cond = toUnique(state, cond);
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(cond)) {
    // Somewhat gross to create these all the time, but fine till we
    // switch to an internal rep.
    const llvm::IntegerType *Ty =
      cast<IntegerType>(si->getCondition()->getType());
    ConstantInt *ci = ConstantInt::get(Ty, CE->getZExtValue());
    unsigned index = si->findCaseValue(ci);

    // We need to have the same set of targets to pass to fork() in case
    // toUnique fails/times out on replay (it's happened before...)
    for(unsigned i = 0; i < cases; ++i) {
      // default to infeasible target
      std::map<BasicBlock*, ref<Expr> >::iterator it =
        targets.insert(std::make_pair(si->getSuccessor(i),
        ConstantExpr::alloc(0, Expr::Bool))).first;
      // set unique target as feasible
      if (i == index)
        it->second = ConstantExpr::alloc(1, Expr::Bool);
    }
  } else {
    ref<Expr> isDefault = ConstantExpr::alloc(1, Expr::Bool);
    for (unsigned i=1; i<cases; ++i) {
      ref<Expr> value = evalConstant(si->getCaseValue(i));
      ref<Expr> match = EqExpr::create(cond, value);

      // default case is the AND of all the complements
      isDefault = AndExpr::create(isDefault, Expr::createIsZero(match));

      // multiple values may lead to same BasicBlock; only fork these once
      std::pair<std::map<BasicBlock*, ref<Expr> >::iterator,bool> ins =
        targets.insert(std::make_pair(si->getSuccessor(i), match));
      if (!ins.second)
        ins.first->second = OrExpr::create(match, ins.first->second);
    }
    // include default case
    targets.insert(std::make_pair(si->getSuccessor(0), isDefault));
  }

  // prepare vectors for fork call
  caseConds.resize(targets.size());
  caseDests.resize(targets.size());
  unsigned index = 0;
  for (std::map<BasicBlock*, ref<Expr> >::iterator mit = targets.begin();
       mit != targets.end(); ++mit) {
    caseDests[index] = (*mit).first;
    caseConds[index] = (*mit).second;
    index++;
  }

  resultStates = fork(state, caseConds.size(), caseConds.data(), false);
  assert(resultStates.size() == caseConds.size());

  for(index = 0; index < resultStates.size(); index++) {
    ExecutionState *es = resultStates[index];
    if (!es) continue;

    BasicBlock *destBlock = caseDests[index];
    KFunction *kf = state.stack.back().kf;
    unsigned entry = kf->basicBlockEntry[destBlock];

    if (es->isCompactForm && kf->trackCoverage &&
        theStatisticManager->getIndexedValue(stats::uncoveredInstructions,
        kf->instructions[entry]->info->id)) {
      ExecutionState *newState = es->reconstitute(*initialStateCopy);
      replaceStateImmForked(es, newState);
      es = newState;
    }

    if (!es->isCompactForm) transferToBasicBlock(destBlock, bb, *es);
   
    // Update coverage stats
    if (kf->trackCoverage &&
        theStatisticManager->getIndexedValue(stats::uncoveredInstructions,
        kf->instructions[entry]->info->id)) {
      es->coveredNew = true;
      es->instsSinceCovNew = 1;
    }
  }
}

void Executor::instExtractElement(ExecutionState& state, KInstruction* ki)
{
	/* extract element has two parametres:
	 * 1. source vector (v)
	 * 2. extraction index (idx)
	 * returns v[idx]
	 */
	ref<Expr> in_v = eval(ki, 0, state).value;
	ref<Expr> in_idx = eval(ki, 1, state).value;
	ConstantExpr* in_idx_ce = dynamic_cast<ConstantExpr*>(in_idx.get());
	assert (in_idx_ce && "NON-CONSTANT EXTRACT ELEMENT IDX. PUKE");
	uint64_t idx = in_idx_ce->getZExtValue();

	/* instruction has types of vectors embedded in its operands */
	ExtractElementInst*	eei = cast<ExtractElementInst>(ki->inst);
	assert (eei != NULL);

	const VectorType*	vt;
	vt = dynamic_cast<const VectorType*>(eei->getOperand(0)->getType());
	unsigned int		v_elem_c = vt->getNumElements();
	unsigned int		v_elem_sz = vt->getBitWidth() / v_elem_c;

	assert (idx < v_elem_c && "ExtrctElement idx overflow");
	ref<Expr>		out_val;
	out_val = ExtractExpr::create(in_v, idx*v_elem_sz, v_elem_sz);
	bindLocal(ki, state, out_val);
}

void Executor::instShuffleVector(ExecutionState& state, KInstruction* ki)
{
	/* shuffle vector has three parameters:
	 * 1. < in_vector | 
	 * 2.             | in_vector >
	 * 3. < perm vect >
	 * 	Permutation vector
	 */
	ref<Expr> in_v_lo = eval(ki, 0, state).value;
	ref<Expr> in_v_hi = eval(ki, 1, state).value;
	ref<Expr> in_v_perm = eval(ki, 2, state).value;
	ConstantExpr* in_v_perm_ce = dynamic_cast<ConstantExpr*>(in_v_perm.get());
	assert (in_v_perm_ce != NULL && "WE HAVE NON-CONST SHUFFLES?? UGH.");

	/* instruction has types of vectors embedded in its operands */
	ShuffleVectorInst*	si = cast<ShuffleVectorInst>(ki->inst);
	assert (si != NULL);
	const VectorType*	vt = si->getType();
	unsigned int		v_elem_c = vt->getNumElements();
	unsigned int		v_elem_sz = vt->getBitWidth() / v_elem_c;

	ref<Expr>		out_val;
	for (unsigned int i = 0; i < v_elem_c; i++) {
		ref<ConstantExpr>	v_idx;
		ref<Expr>		ext;
		uint64_t		idx;

		v_idx = in_v_perm_ce->Extract(i*v_elem_sz, v_elem_sz);
		idx = v_idx->getZExtValue();
		assert (idx < 2*v_elem_c && "Shuffle permutation out of range");
		if (idx < v_elem_c) {
			ext = ExtractExpr::create(
				in_v_lo, v_elem_sz*idx, v_elem_sz);
		} else {
			idx -= v_elem_c;
			ext = ExtractExpr::create(
				in_v_hi, v_elem_sz*idx, v_elem_sz);
		}

		fprintf(stderr, "[%d]: ", i); v_idx->dump();
		if (i == 0) out_val = ext;
		else out_val = ConcatExpr::create(out_val, ext);
	}

	bindLocal(ki, state, out_val);
}

void Executor::instUnwind(ExecutionState& state)
{
  while (1) {
    KInstruction *kcaller = state.getCaller();
    state.popFrame();

    if (statsTracker) statsTracker->framePopped(state);

    if (state.stack.empty()) {
      terminateStateOnExecError(state, "unwind from initial stack frame");
      return;
    }

    Instruction *caller = kcaller->inst;
    if (InvokeInst *ii = dyn_cast<InvokeInst>(caller)) {
      transferToBasicBlock(ii->getUnwindDest(), caller->getParent(), state);
      return;
    }
  }
}

bool Executor::isFPPredicateMatched(
  APFloat::cmpResult CmpRes, CmpInst::Predicate pred)
{
  switch(pred) {
  // Predicates which only care about whether or not the operands are NaNs.
  case FCmpInst::FCMP_ORD: return CmpRes != APFloat::cmpUnordered;
  case FCmpInst::FCMP_UNO: return CmpRes == APFloat::cmpUnordered;
  // Ordered comparisons return false if either operand is NaN.  Unordered
  // comparisons return true if either operand is NaN.
  case FCmpInst::FCMP_UEQ: return CmpRes == APFloat::cmpUnordered;
  case FCmpInst::FCMP_OEQ: return CmpRes == APFloat::cmpEqual;
  case FCmpInst::FCMP_UGT: return CmpRes == APFloat::cmpUnordered;
  case FCmpInst::FCMP_OGT: return CmpRes == APFloat::cmpGreaterThan;
  case FCmpInst::FCMP_UGE: return CmpRes == APFloat::cmpUnordered;
  case FCmpInst::FCMP_OGE:
    return CmpRes == APFloat::cmpGreaterThan || CmpRes == APFloat::cmpEqual;
  case FCmpInst::FCMP_ULT: return CmpRes == APFloat::cmpUnordered;
  case FCmpInst::FCMP_OLT: return CmpRes == APFloat::cmpLessThan;
  case FCmpInst::FCMP_ULE: return CmpRes == APFloat::cmpUnordered;
  case FCmpInst::FCMP_OLE:
    return CmpRes == APFloat::cmpLessThan || CmpRes == APFloat::cmpEqual;
  case FCmpInst::FCMP_UNE:
    return CmpRes == APFloat::cmpUnordered || CmpRes != APFloat::cmpEqual;
  case FCmpInst::FCMP_ONE:
    return CmpRes != APFloat::cmpUnordered && CmpRes != APFloat::cmpEqual;
  default: assert(0 && "Invalid FCMP predicate!");
  case FCmpInst::FCMP_FALSE: return false;
  case FCmpInst::FCMP_TRUE: return true;
  }
  return false;
}

void Executor::executeInstruction(ExecutionState &state, KInstruction *ki)
{
  Instruction *i = ki->inst;
  switch (i->getOpcode()) {
    // Control flow
  case Instruction::Ret: 
    if (WriteTraces) {
      state.exeTraceMgr.addEvent(new FunctionReturnTraceEvent(state, ki));
    }
    instRet(state, ki);
    break;

  case Instruction::Unwind:
    instUnwind(state);
    break;

  case Instruction::Br: instBranch(state, ki); break;
  case Instruction::Switch: instSwitch(state, ki); break;
  case Instruction::Unreachable:
    // Note that this is not necessarily an internal bug, llvm will
    // generate unreachable instructions in cases where it knows the
    // program will crash. So it is effectively a SEGV or internal
    // error.
    terminateStateOnExecError(state, "reached \"unreachable\" instruction");
    break;

  case Instruction::Invoke:
  case Instruction::Call:
    instCall(state, ki);
    break;
  case Instruction::PHI: {
    ref<Expr> result = eval(ki, state.incomingBBIndex * 2, state).value;
    bindLocal(ki, state, result);
    break;
  }

    // Special instructions
  case Instruction::Select: {
    SelectInst *SI = cast<SelectInst>(ki->inst);
    assert(SI->getCondition() == SI->getOperand(0) &&
           "Wrong operand index!");
    ref<Expr> cond = eval(ki, 0, state).value;
    ref<Expr> tExpr = eval(ki, 1, state).value;
    ref<Expr> fExpr = eval(ki, 2, state).value;
    ref<Expr> result = SelectExpr::create(cond, tExpr, fExpr);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::VAArg:
    terminateStateOnExecError(state, "unexpected VAArg instruction");
    break;

  // Arithmetic / logical
#define INST_ARITHOP(x,y)                             \
  case Instruction::x : {                             \
    ref<Expr> left = eval(ki, 0, state).value;        \
    ref<Expr> right = eval(ki, 1, state).value;       \
    bindLocal(ki, state, y::create(left, right));     \
    break; \
  }

  INST_ARITHOP(Add,AddExpr)
  INST_ARITHOP(Sub,SubExpr)
  INST_ARITHOP(Mul,MulExpr)
  INST_ARITHOP(UDiv,UDivExpr)
  INST_ARITHOP(SDiv,SDivExpr)
  INST_ARITHOP(URem,URemExpr)
  INST_ARITHOP(SRem,SRemExpr)
  INST_ARITHOP(And,AndExpr)
  INST_ARITHOP(Or,OrExpr)
  INST_ARITHOP(Xor,XorExpr)
  INST_ARITHOP(Shl,ShlExpr)
  INST_ARITHOP(LShr,LShrExpr)
  INST_ARITHOP(AShr,AShrExpr)


  case Instruction::ICmp: instCmp(state, ki); break;

  case Instruction::Load: {
    ref<Expr> base = eval(ki, 0, state).value;
    executeMemoryOperation(state, false, base, 0, ki);
    break;
  }
  case Instruction::Store: {
    ref<Expr> base = eval(ki, 1, state).value;
    ref<Expr> value = eval(ki, 0, state).value;
    executeMemoryOperation(state, true, base, value, 0);
    break;
  }

  case Instruction::GetElementPtr: {
    KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(ki);
    ref<Expr> base = eval(ki, 0, state).value;

    foreach (it, kgepi->indices.begin(), kgepi->indices.end()) {
      uint64_t elementSize = it->second;
      ref<Expr> index = eval(ki, it->first, state).value;
      base = AddExpr::create(base,
                             MulExpr::create(Expr::createCoerceToPointerType(index),
                                             Expr::createPointer(elementSize)));
    }
    if (kgepi->offset)
      base = AddExpr::create(base, Expr::createPointer(kgepi->offset));
    bindLocal(ki, state, base);
    break;
  }

    // Conversion
  case Instruction::Trunc: {
    CastInst *ci = cast<CastInst>(i);
    ref<Expr> result = ExtractExpr::create(eval(ki, 0, state).value,
                                           0,
                                           getWidthForLLVMType(ci->getType()));
    bindLocal(ki, state, result);
    break;
  }
  case Instruction::ZExt: {
    CastInst *ci = cast<CastInst>(i);
    ref<Expr> result = ZExtExpr::create(eval(ki, 0, state).value,
                                        getWidthForLLVMType(ci->getType()));
    bindLocal(ki, state, result);
    break;
  }
  case Instruction::SExt: {
    CastInst *ci = cast<CastInst>(i);
    ref<Expr> result = SExtExpr::create(eval(ki, 0, state).value,
                                        getWidthForLLVMType(ci->getType()));
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::IntToPtr: {
    CastInst *ci = cast<CastInst>(i);
    Expr::Width pType = getWidthForLLVMType(ci->getType());
    ref<Expr> arg = eval(ki, 0, state).value;
    bindLocal(ki, state, ZExtExpr::create(arg, pType));
    break;
  }
  case Instruction::PtrToInt: {
    CastInst *ci = cast<CastInst>(i);
    Expr::Width iType = getWidthForLLVMType(ci->getType());
    ref<Expr> arg = eval(ki, 0, state).value;
    bindLocal(ki, state, ZExtExpr::create(arg, iType));
    break;
  }

  case Instruction::BitCast: {
    ref<Expr> result = eval(ki, 0, state).value;
    bindLocal(ki, state, result);
    break;
  }

    // Floating point instructions

  case Instruction::FAdd: {
    ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value,
                                         "floating point");
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FAdd operation");

    llvm::APFloat Res(left->getAPValue());
    Res.add(APFloat(right->getAPValue()), APFloat::rmNearestTiesToEven);
    bindLocal(ki, state, ConstantExpr::alloc(Res.bitcastToAPInt()));
    break;
  }

  case Instruction::FSub: {
    ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value,
                                         "floating point");
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FSub operation");

    llvm::APFloat Res(left->getAPValue());
    Res.subtract(APFloat(right->getAPValue()), APFloat::rmNearestTiesToEven);
    bindLocal(ki, state, ConstantExpr::alloc(Res.bitcastToAPInt()));
    break;
  }

  case Instruction::FMul: {
    ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value,
                                         "floating point");
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FMul operation");

    llvm::APFloat Res(left->getAPValue());
    Res.multiply(APFloat(right->getAPValue()), APFloat::rmNearestTiesToEven);
    bindLocal(ki, state, ConstantExpr::alloc(Res.bitcastToAPInt()));
    break;
  }

  case Instruction::FDiv: {
    ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value,
                                         "floating point");
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FDiv operation");

    llvm::APFloat Res(left->getAPValue());
    Res.divide(APFloat(right->getAPValue()), APFloat::rmNearestTiesToEven);
    bindLocal(ki, state, ConstantExpr::alloc(Res.bitcastToAPInt()));
    break;
  }

  case Instruction::FRem: {
    ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value,
                                         "floating point");
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FRem operation");

    llvm::APFloat Res(left->getAPValue());
    Res.mod(APFloat(right->getAPValue()), APFloat::rmNearestTiesToEven);
    bindLocal(ki, state, ConstantExpr::alloc(Res.bitcastToAPInt()));
    break;
  }

  case Instruction::FPTrunc: {
    FPTruncInst *fi = cast<FPTruncInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    if (!fpWidthToSemantics(arg->getWidth()) || resultType > arg->getWidth())
      return terminateStateOnExecError(state, "Unsupported FPTrunc operation");

    llvm::APFloat Res(arg->getAPValue());
    bool losesInfo = false;
    Res.convert(*fpWidthToSemantics(resultType),
                llvm::APFloat::rmNearestTiesToEven,
                &losesInfo);
    bindLocal(ki, state, ConstantExpr::alloc(Res));
    break;
  }

  case Instruction::FPExt: {
    FPExtInst *fi = cast<FPExtInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    if (!fpWidthToSemantics(arg->getWidth()) || arg->getWidth() > resultType)
      return terminateStateOnExecError(state, "Unsupported FPExt operation");

    llvm::APFloat Res(arg->getAPValue());
    bool losesInfo = false;
    Res.convert(*fpWidthToSemantics(resultType),
                llvm::APFloat::rmNearestTiesToEven,
                &losesInfo);
    bindLocal(ki, state, ConstantExpr::alloc(Res));
    break;
  }

  case Instruction::FPToUI: {
    FPToUIInst *fi = cast<FPToUIInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                       "floating point");
    if (!fpWidthToSemantics(arg->getWidth()) || resultType > 64)
      return terminateStateOnExecError(state, "Unsupported FPToUI operation");

    llvm::APFloat Arg(arg->getAPValue());
    uint64_t value = 0;
    bool isExact = true;
    Arg.convertToInteger(&value, resultType, false,
                         llvm::APFloat::rmTowardZero, &isExact);
    bindLocal(ki, state, ConstantExpr::alloc(value, resultType));
    break;
  }

  case Instruction::FPToSI: {
    FPToSIInst *fi = cast<FPToSIInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                       "floating point");
    if (!fpWidthToSemantics(arg->getWidth()) || resultType > 64)
      return terminateStateOnExecError(state, "Unsupported FPToSI operation");

    llvm::APFloat Arg(arg->getAPValue());
    uint64_t value = 0;
    bool isExact = true;
    Arg.convertToInteger(&value, resultType, false,
                         llvm::APFloat::rmTowardZero, &isExact);
    bindLocal(ki, state, ConstantExpr::alloc(value, resultType));
    break;
  }

  case Instruction::UIToFP: {
    UIToFPInst *fi = cast<UIToFPInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                       "floating point");
    const llvm::fltSemantics *semantics = fpWidthToSemantics(resultType);
    if (!semantics)
      return terminateStateOnExecError(state, "Unsupported UIToFP operation");
    llvm::APFloat f(*semantics, 0);
    f.convertFromAPInt(arg->getAPValue(), false,
                       llvm::APFloat::rmNearestTiesToEven);

    bindLocal(ki, state, ConstantExpr::alloc(f));
    break;
  }

  case Instruction::SIToFP: {
    SIToFPInst *fi = cast<SIToFPInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                       "floating point");
    const llvm::fltSemantics *semantics = fpWidthToSemantics(resultType);
    if (!semantics)
      return terminateStateOnExecError(state, "Unsupported SIToFP operation");
    llvm::APFloat f(*semantics, 0);
    f.convertFromAPInt(arg->getAPValue(), true,
                       llvm::APFloat::rmNearestTiesToEven);

    bindLocal(ki, state, ConstantExpr::alloc(f));
    break;
  }

  case Instruction::FCmp: {
    FCmpInst *fi = cast<FCmpInst>(i);
    ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value,
                                         "floating point");
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FCmp operation");

    APFloat LHS(left->getAPValue());
    APFloat RHS(right->getAPValue());
    APFloat::cmpResult CmpRes = LHS.compare(RHS);

    bindLocal(ki, state,
      ConstantExpr::alloc(
        isFPPredicateMatched(CmpRes, fi->getPredicate()),
        Expr::Bool));
    break;
  }

    // Other instructions...
    // Unhandled
  case Instruction::ExtractElement:
    instExtractElement(state, ki);
    break;
  case Instruction::InsertElement:
    terminateStateOnError(
      state, "XXX vector instructions unhandled", "xxx.err");
    break;

  case Instruction::ShuffleVector:
    instShuffleVector(state, ki);
    break;

  default:
    terminateStateOnExecError(state, "illegal instruction");
    break;
  }
}

void Executor::removePTreeState(
  ExecutionState* es, ExecutionState** root_to_be_removed)
{
  ExecutionState* ns;
  std::map<ExecutionState*, std::vector<SeedInfo> >::iterator it3;
  it3 = seedMap.find(es);
  if (it3 != seedMap.end()) seedMap.erase(it3);

  if (es->ptreeNode == processTree->root) {
    assert(root_to_be_removed);
    *root_to_be_removed = es;
    return;
  }
  assert(es->ptreeNode->data == es);

  ns = stateManager->getReplacedState(es);
  if (!ns) {
    processTree->remove(es->ptreeNode);
  } else {
    // replace the placeholder state in the process tree
    ns->ptreeNode = es->ptreeNode;
    ns->ptreeNode->data = ns;
    processTree->update(ns->ptreeNode, PTree::WeightCompact, !ns->isCompactForm);
  }
  delete es;
}

void Executor::removeRoot(ExecutionState* es)
{
  ExecutionState* ns = stateManager->getReplacedState(es);

  if (!ns) {
    delete processTree->root->data;
    processTree->root->data = 0;
    return;
  }

  // replace the placeholder state in the process tree
  ns->ptreeNode = es->ptreeNode;
  ns->ptreeNode->data = ns;

  processTree->update(ns->ptreeNode, PTree::WeightCompact, !ns->isCompactForm);
  delete es;
}

void Executor::bindInstructionConstants(KInstruction *KI)
{
  GetElementPtrInst *gepi = dyn_cast<GetElementPtrInst>(KI->inst);
  if (!gepi)
    return;

  KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(KI);
  ref<ConstantExpr> constantOffset =
    ConstantExpr::alloc(0, Context::get().getPointerWidth());
  uint64_t index = 1;
  foreach (ii, gep_type_begin(gepi), gep_type_end(gepi)) {
    if (const StructType *st = dyn_cast<StructType>(*ii)) {
      const StructLayout *sl = target_data->getStructLayout(st);
      const ConstantInt *ci = cast<ConstantInt>(ii.getOperand());
      uint64_t addend = sl->getElementOffset((unsigned) ci->getZExtValue());
      constantOffset = constantOffset->Add(
        ConstantExpr::alloc(addend, Context::get().getPointerWidth()));
    } else {
      const SequentialType *st = cast<SequentialType>(*ii);
      uint64_t elementSize =
        target_data->getTypeStoreSize(st->getElementType());
      Value *operand = ii.getOperand();
      if (Constant *c = dyn_cast<Constant>(operand)) {
        ref<ConstantExpr> index =
          evalConstant(c)->ZExt(Context::get().getPointerWidth());
        ref<ConstantExpr> addend =
          index->Mul(ConstantExpr::alloc(elementSize,
                                         Context::get().getPointerWidth()));
        constantOffset = constantOffset->Add(addend);
      } else {
        kgepi->indices.push_back(std::make_pair(index, elementSize));
      }
    }
    index++;
  }
  kgepi->offset = constantOffset->getZExtValue();
}

void Executor::killStates(ExecutionState* &state)
{
  // just guess at how many to kill
  uint64_t numStates = stateManager->size();
  uint64_t mbs = getMemUsageMB();
  unsigned toKill = std::max((uint64_t)1, numStates - (numStates*MaxMemory)/mbs);
  assert (mbs > MaxMemory);

  klee_warning(
    "killing %u states (over memory cap). Total states = %ld.",
    toKill, numStates);

  std::vector<ExecutionState*> arr(
    stateManager->begin(),
    stateManager->end());

  // use priority ordering for selecting which states to kill
  std::partial_sort(
    arr.begin(), arr.begin() + toKill, arr.end(), KillOrCompactOrdering());
  for (unsigned i = 0; i < toKill; ++i) {
    terminateStateEarly(*arr[i], "memory limit");
    if (state == arr[i]) state = NULL;
  }
  klee_message("Killed %u states.", toKill);
}

void Executor::runState(ExecutionState* &state)
{
  state->lastChosen = stats::instructions;

  KInstruction *ki = state->pc;
  assert(ki);

  stepInstruction(*state);
  executeInstruction(*state, ki);
  processTimers(state, MaxInstructionTime);
  handleMemoryUtilization(state);
}

void Executor::handleMemoryUtilization(ExecutionState* &state)
{
  if (!(MaxMemory && (stats::instructions & 0xFFFF) == 0))
    return;

  // We need to avoid calling GetMallocUsage() often because it
  // is O(elts on freelist). This is really bad since we start
  // to pummel the freelist once we hit the memory cap.
  uint64_t mbs = getMemUsageMB();
 
  if (mbs < 0.9*MaxMemory) {
    atMemoryLimit = false;
    return;
  }

  if (mbs <= MaxMemory) return;

  /*  (mbs > MaxMemory) */
  atMemoryLimit = true;
  onlyNonCompact = true;

  if (mbs <= MaxMemory + 100) return;

  /* Ran memory to the roof. FLIP OUT. */
  if (!ReplayInhibitedForks ||
      /* resort to killing states if the recent compacting
         didn't help to reduce the memory usage */
      stats::instructions-lastMemoryLimitOperationInstructions <= 0x20000)
  {
    killStates(state);
  } else {
    stateManager->compactStates(state, MaxMemory);
  }
  lastMemoryLimitOperationInstructions = stats::instructions;
}

void Executor::seedRunOne(ExecutionState* &lastState)
{
  std::map<ExecutionState*, std::vector<SeedInfo> >::iterator it;
 
  it = seedMap.upper_bound(lastState);
  if (it == seedMap.end()) it = seedMap.begin();
  lastState = it->first;

  unsigned numSeeds = it->second.size();
  ExecutionState &state = *lastState;
  KInstruction *ki = state.pc;

  stepInstruction(state);
  executeInstruction(state, ki);
  processTimers(&state, MaxInstructionTime * numSeeds);
  updateStates(&state);
}

bool Executor::seedRun(ExecutionState& initialState)
{
  ExecutionState *lastState = 0;
  double lastTime, startTime = lastTime = util::estWallTime();
  std::vector<SeedInfo> &v = seedMap[&initialState];
 
  foreach (it, usingSeeds->begin(), usingSeeds->end()) {
    v.push_back(SeedInfo(*it));
  }

  int lastNumSeeds = usingSeeds->size()+10;
  while (!seedMap.empty() && !haltExecution) {
    double time;

    seedRunOne(lastState);

    /* every 1000 instructions, check timeouts, seed counts */
    if ((stats::instructions % 1000) != 0) continue;

    unsigned numSeeds = 0;
    unsigned numStates = seedMap.size();
    foreach (it, seedMap.begin(), seedMap.end()) {
    	numSeeds += it->second.size();
    }

    time = util::estWallTime();
    if (SeedTime>0. && time > startTime + SeedTime) {
      klee_warning("seed time expired, %d seeds remain over %d states",
                   numSeeds, numStates);
      break;
    } else if ((int)numSeeds<=lastNumSeeds-10 || time >= lastTime+10) {
      lastTime = time;
      lastNumSeeds = numSeeds;         
      klee_message("%d seeds remaining over: %d states", numSeeds, numStates);
    }
  }

  if (haltExecution) return false;

  klee_message("seeding done (%d states remain)", (int) stateManager->size());

  // XXX total hack, just because I like non uniform better but want
  // seed results to be equally weighted.
  stateManager->setWeights(1.0);
  return true;
}

void Executor::replayPathsIntoStates(ExecutionState& initialState)
{
  assert (replayPaths);
  foreach (it, replayPaths->begin(), replayPaths->end()) {
    ExecutionState *newState = new ExecutionState(initialState);
    foreach (it2, (*it).begin(), (*it).end()) {
      newState->branchDecisionsSequence.push_back(*it2);
    }
    newState->replayBranchIterator = newState->branchDecisionsSequence.begin();
    newState->ptreeNode->data = 0;
    newState->isReplay = true;
    splitProcessTree(newState->ptreeNode, &initialState, newState);
    stateManager->add(newState);
  }
}

void Executor::run(ExecutionState &initialState)
{
  // Delay init till now so that ticks don't accrue during
  // optimization and such.
  initTimers();

  if (ReplayInhibitedForks) {
    initialStateCopy = new ExecutionState(initialState);
  } else
    initialStateCopy = NULL;

  if (replayPaths) replayPathsIntoStates(initialState);
  stateManager->setInitialState(this, &initialState, replayPaths);

  if (usingSeeds) {
    if (!seedRun(initialState)) goto dump;
    if (OnlySeed) goto dump;
  }

  stateManager->setupSearcher(this);

  runLoop();

  stateManager->teardownUserSearcher();

dump:
  if (stateManager->empty()) goto done;
  std::cerr << "KLEE: halting execution, dumping remaining states\n";
  foreach (it, stateManager->begin(), stateManager->end()) {
    ExecutionState &state = **it;
    stepInstruction(state); // keep stats rolling
    if (DumpStatesOnHalt)
      terminateStateEarly(state, "execution halting");
    else
      terminateState(state);
  }
  updateStates(0);

done:
  if (initialStateCopy) delete initialStateCopy;
}

void Executor::runLoop(void)
{
  while (!stateManager->empty() && !haltExecution) {
    ExecutionState *state = stateManager->selectState(!onlyNonCompact);

    assert (state != NULL && "State man not empty, but selectState is?");
    /* decompress state if compact */
    if (state->isCompactForm) {
      ExecutionState* newState = state->reconstitute(*initialStateCopy);
      stateManager->replaceState(state, newState);
      updateStates(state);
      state = newState;
    }

    runState(state);
    updateStates(state);
  }
}

void Executor::updateStates(ExecutionState* current)
{
  stateManager->updateStates(this, current);
  if (stateManager->getNonCompactStateCount() == 0
    && !stateManager->empty())
  {
    onlyNonCompact = false;
  }
}

std::string Executor::getAddressInfo(ExecutionState &state,
                                     ref<Expr> address) const{
  std::ostringstream info;
  info << "\taddress: " << address << "\n";
  uint64_t example;
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(address)) {
    example = CE->getZExtValue();
  } else {
    ref<ConstantExpr> value;
    bool success = solver->getValue(state, address, value);
    assert(success && "FIXME: Unhandled solver failure");
    (void) success;
    example = value->getZExtValue();
    info << "\texample: " << example << "\n";
    std::pair< ref<Expr>, ref<Expr> > res = solver->getRange(state, address);
    info << "\trange: [" << res.first << ", " << res.second <<"]\n";
  }
 
  MemoryObject hack((unsigned) example);   
  MemoryMap::iterator lower = state.addressSpace.objects.upper_bound(&hack);
  info << "\tnext: ";
  if (lower==state.addressSpace.objects.end()) {
    info << "none\n";
  } else {
    const MemoryObject *mo = lower->first;
    std::string alloc_info;
    mo->getAllocInfo(alloc_info);
    info << "object at " << mo->address
         << " of size " << mo->size << "\n"
         << "\t\t" << alloc_info << "\n";
  }
  if (lower!=state.addressSpace.objects.begin()) {
    --lower;
    info << "\tprev: ";
    if (lower==state.addressSpace.objects.end()) {
      info << "none\n";
    } else {
      const MemoryObject *mo = lower->first;
      std::string alloc_info;
      mo->getAllocInfo(alloc_info);
      info << "object at " << mo->address
           << " of size " << mo->size << "\n"
           << "\t\t" << alloc_info << "\n";
    }
  }

  return info.str();
}

void Executor::terminateState(ExecutionState &state) {
  if (replayOut && replayPosition!=replayOut->numObjects) {
    klee_warning_once(replayOut,
                      "replay did not consume all objects in test input.");
  }

  interpreterHandler->incPathsExplored();

  if (!stateManager->isAddedState(&state)) {
    state.pc = state.prevPC;
    stateManager->remove(&state); /* put on remove list */
  } else {
    // never reached searcher, just delete immediately
    std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it3;
    it3 = seedMap.find(&state);
    if (it3 != seedMap.end()) seedMap.erase(it3);
    stateManager->dropAdded(&state);
    processTree->remove(state.ptreeNode);
    delete &state;
  }
}

void Executor::terminateStateEarly(ExecutionState &state,
                                   const Twine &message) {
  if (!OnlyOutputStatesCoveringNew || state.coveredNew ||
      (AlwaysOutputSeeds && seedMap.count(&state)))
    interpreterHandler->processTestCase(state, (message + "\n").str().c_str(),
                                        "early");
  terminateState(state);
}

void Executor::terminateStateOnExit(ExecutionState &state) {
  if (!OnlyOutputStatesCoveringNew || state.coveredNew ||
      (AlwaysOutputSeeds && seedMap.count(&state)))
    interpreterHandler->processTestCase(state, 0, 0);
  terminateState(state);
}

void Executor::terminateStateOnError(ExecutionState &state,
                                     const llvm::Twine &messaget,
                                     const char *suffix,
                                     const llvm::Twine &info) {
  std::string message = messaget.str();
  static std::set< std::pair<Instruction*, std::string> > emittedErrors;
  const InstructionInfo &ii = *state.prevPC->info;
 
  if (!(EmitAllErrors ||
      emittedErrors.insert(std::make_pair(state.prevPC->inst, message)).second))
  {
    terminateState(state);
    return;
  }


  if (ii.file != "") {
    klee_message("ERROR: %s:%d: %s", ii.file.c_str(), ii.line, message.c_str());
  } else {
    klee_message("ERROR: %s", message.c_str());
  }
  if (!EmitAllErrors)
    klee_message("NOTE: now ignoring this error at this location");
 
  std::ostringstream msg;
  msg << "Error: " << message << "\n";
  if (ii.file != "") {
    msg << "File: " << ii.file << "\n";
    msg << "Line: " << ii.line << "\n";
  }

  msg << "Stack: \n";
  state.dumpStack(msg);

  std::string info_str = info.str();
  if (info_str != "") msg << "Info: \n" << info_str;
  interpreterHandler->processTestCase(state, msg.str().c_str(), suffix);

  terminateState(state);
}

/***/

ref<Expr> Executor::replaceReadWithSymbolic(
  ExecutionState &state, ref<Expr> e)
{
  static unsigned id;
  unsigned n = interpreterOpts.MakeConcreteSymbolic;

  if (!n || replayOut || replayPaths) return e;

  // right now, we don't replace symbolics (is there any reason too?)
  if (!isa<ConstantExpr>(e)) return e;

  /* XXX why random? */
  if (n != 1 && random() %  n) return e;

  // create a new fresh location, assert it is equal to concrete value in e
  // and return it.
 
  const Array *array =
    new Array("rrws_arr" + llvm::utostr(++id),
              MallocKey(Expr::getMinBytesForWidth(e->getWidth())));
  ref<Expr> res = Expr::createTempRead(array, e->getWidth());
  ref<Expr> eq = NotOptimizedExpr::create(EqExpr::create(e, res));
  std::cerr << "Making symbolic: " << eq << "\n";
  state.addConstraint(eq);
  return res;
}

ObjectState *Executor::bindObjectInState(
  ExecutionState &state,
  const MemoryObject *mo,
  bool isLocal,
  const Array *array)
{
  ObjectState *os;
 
  if (array) os = new ObjectState(mo, array);
  else os = new ObjectState(mo);

  state.bindObject(mo, os);

  // Its possible that multiple bindings of the same mo in the state
  // will put multiple copies on this list, but it doesn't really
  // matter because all we use this list for is to unbind the object
  // on function return.
  if (isLocal) state.stack.back().allocas.push_back(mo);

  return os;
}

void Executor::resolveExact(ExecutionState &state,
                            ref<Expr> p,
                            ExactResolutionList &results,
                            const std::string &name) {
  // XXX we may want to be capping this?
  ResolutionList rl;
  state.addressSpace.resolve(state, solver, p, rl);
 
  ExecutionState *unbound = &state;
  for (ResolutionList::iterator it = rl.begin(), ie = rl.end();
       it != ie; ++it) {
    ref<Expr> inBounds = EqExpr::create(p, it->first->getBaseExpr());
   
    StatePair branches = fork(*unbound, inBounds, true);
   
    if (branches.first)
      results.push_back(std::make_pair(*it, branches.first));

    unbound = branches.second;
    if (!unbound) // Fork failure
      break;
  }

  if (unbound) {
    terminateStateOnError(*unbound,
                          "memory error: invalid pointer: " + name,
                          "ptr.err",
                          getAddressInfo(*unbound, p));
  }
}

/* handles a memop that can be immediately resolved */
bool Executor::memOpFast(
  ExecutionState& state,
  bool isWrite,
  ref<Expr> address,
  ref<Expr> value,
  KInstruction* target)
{
  Expr::Width type = (isWrite ? value->getWidth() :
                     getWidthForLLVMType(target->inst->getType()));
  unsigned bytes = Expr::getMinBytesForWidth(type);
  ObjectPair op;
  ref<Expr> offset;
  bool inBounds, success;

  /* can op be resolved to a single value? */
  if (!state.addressSpace.resolveOne(state, solver, address, op, success)) {
    address = toConstant(state, address, "resolveOne failure");
    success = state.addressSpace.resolveOne(cast<ConstantExpr>(address), op);
  }

  if (!success) return false;

  // fast path: single in-bounds resolution.
  const MemoryObject *mo = op.first;
  const ObjectState *os = op.second;

  if (MaxSymArraySize && mo->size>=MaxSymArraySize) {
    address = toConstant(state, address, "max-sym-array-size");
  }
 
  offset = mo->getOffsetExpr(address);

  /* verify access is in bounds */
  success = solver->mustBeTrue(
    state, mo->getBoundsCheckOffset(offset, bytes), inBounds);
  if (!success) {
    state.pc = state.prevPC;
    terminateStateEarly(state, "query timed out");
    return true;
  }

  if (!inBounds) return false;

  if (isWrite) {
    if (os->readOnly) {
      terminateStateOnError(state,
                            "memory error: object read only",
                            "readonly.err");
    } else {
      ObjectState *wos = state.addressSpace.getWriteable(mo, os);
      state.write(wos, offset, value);
    }         
  } else {
    ref<Expr> result = state.read(os, offset, type);
    if (interpreterOpts.MakeConcreteSymbolic)
      result = replaceReadWithSymbolic(state, result);
    bindLocal(target, state, result);
  }

  return true;
}

ExecutionState* Executor::getUnboundState(
  ExecutionState* unbound,
  ObjectPair& resolution,
  bool isWrite,
  ref<Expr> address,
  unsigned bytes,
  Expr::Width& type,
  ref<Expr> value,
  KInstruction* target)
{
  const MemoryObject *mo;
  const ObjectState *os;

  mo = resolution.first;
  os = resolution.second;
  ref<Expr> inBounds = mo->getBoundsCheckPointer(address, bytes);

  StatePair branches = fork(*unbound, inBounds, true);
  ExecutionState *bound = branches.first;

  // bound can be 0 on failure or overlapped
  if (bound) {
    if (isWrite) {
      if (os->readOnly) {
        terminateStateOnError(*bound,
                              "memory error: object read only",
                              "readonly.err");
      } else {
        ObjectState *wos = bound->addressSpace.getWriteable(mo, os);
        //wos->write(mo->getOffsetExpr(address), value);
        bound->write(wos, mo->getOffsetExpr(address), value);
      }
    } else {
      //ref<Expr> result = os->read(mo->getOffsetExpr(address), type);
      ref<Expr> result = bound->read(os, mo->getOffsetExpr(address), type);
      bindLocal(target, *bound, result);
    }
  }

  return branches.second;
}

void Executor::memOpError(
  ExecutionState& state,
  bool isWrite,
  ref<Expr> address,
  ref<Expr> value,
  KInstruction* target)
{
  Expr::Width type = (isWrite ? value->getWidth() :
                     getWidthForLLVMType(target->inst->getType()));
  unsigned bytes = Expr::getMinBytesForWidth(type);
  ResolutionList rl; 
  ExecutionState *unbound;
  bool incomplete;

  incomplete = state.addressSpace.resolve(state, solver, address, rl, 0, stpTimeout);

  // XXX there is some query wasteage here. who cares?
  unbound = &state;
  foreach (it, rl.begin(), rl.end()) {
    unbound = getUnboundState(
      unbound, *it, isWrite, address, bytes, type, value, target);
    if (!unbound) break;
  }
 
  if (!unbound) return;

  address->print(std::cerr);

  // XXX should we distinguish out of bounds and overlapped cases?
  if (incomplete) {
    terminateStateEarly(*unbound, "query timed out (resolve)");
  } else {
    terminateStateOnError(*unbound,
                          "memory error: out of bound pointer",
                          "ptr.err",
                          getAddressInfo(*unbound, address));
  }
}


void Executor::executeMemoryOperation(ExecutionState &state,
                                      bool isWrite,
                                      ref<Expr> address,
                                      ref<Expr> value /* undef if read */,
                                      KInstruction *target /* undef if write */)
{
  if (SimplifySymIndices) {
    if (!isa<ConstantExpr>(address))
      address = state.constraints.simplifyExpr(address);
    if (isWrite && !isa<ConstantExpr>(value))
      value = state.constraints.simplifyExpr(value);
  }

  if (memOpFast(state, isWrite, address, value, target)) return;

  // we are on an error path (no resolution, multiple resolution, one
  // resolution with out of bounds)
  memOpError(state, isWrite, address, value, target);
}

void Executor::executeMakeSymbolic(
  ExecutionState &state, const MemoryObject *mo)
{
  executeMakeSymbolic(state, mo, mo->getSizeExpr());
}

void Executor::executeMakeSymbolic(
  ExecutionState &state, const MemoryObject *mo, ref<Expr> len)
{
  if (!replayOut)
    makeSymbolic(state, mo, len);
  else
    makeSymbolicReplay(state, mo, len);
}

// Create a new object state for the memory object (instead of a copy).
void Executor::makeSymbolicReplay(
    ExecutionState& state, const MemoryObject* mo, ref<Expr> len)
{
  ObjectState *os = bindObjectInState(state, mo, false);
  if (replayPosition >= replayOut->numObjects) {
    terminateStateOnError(state, "replay count mismatch", "user.err");
  } else {
    KTestObject *obj = &replayOut->objects[replayPosition++];
    if (obj->numBytes != mo->size) {
      terminateStateOnError(state, "replay size mismatch", "user.err");
    } else {
      for (unsigned i=0; i<mo->size; i++) {
        //os->write8(i, obj->bytes[i]);
        state.write8(os, i, obj->bytes[i]);
      }
    }
  }
}

bool Executor::seedObject(
  ExecutionState& state, SeedInfo& si,
  const MemoryObject* mo, const Array* array)
{
  KTestObject *obj = si.getNextInput(mo, NamedSeedMatching);

  /* if no test objects, create zeroed array object */
  if (!obj) {
    if (ZeroSeedExtension) {
      std::vector<unsigned char> &values = si.assignment.bindings[array];
      values = std::vector<unsigned char>(mo->size, '\0');
    } else if (!AllowSeedExtension) {
      terminateStateOnError(state,
                            "ran out of inputs during seeding",
                            "user.err");
      return false;
    }
    return true;
  }

  /* resize permitted? */
  if (obj->numBytes != mo->size &&
      ((!(AllowSeedExtension || ZeroSeedExtension)
        && obj->numBytes < mo->size) ||
       (!AllowSeedTruncation && obj->numBytes > mo->size)))
  {
    std::stringstream msg;
    msg << "replace size mismatch: "
    << mo->name << "[" << mo->size << "]"
    << " vs " << obj->name << "[" << obj->numBytes << "]"
    << " in test\n";

    terminateStateOnError(state, msg.str(), "user.err");
    return false;
  }

  /* resize object to memory size */
  std::vector<unsigned char> &values = si.assignment.bindings[array];
  values.insert(values.begin(), obj->bytes,
                obj->bytes + std::min(obj->numBytes, mo->size));
  if (ZeroSeedExtension) {
    for (unsigned i=obj->numBytes; i<mo->size; ++i)
      values.push_back('\0');
  }

  return true;
}

void Executor::makeSymbolic(
  ExecutionState& state, const MemoryObject* mo, ref<Expr> len)
{
  static unsigned id = 0;
  const Array *array = new Array("arr" + llvm::utostr(++id), mo->mallocKey, 0, 0);
  array->initRef();
  bindObjectInState(state, mo, false, array);
  state.addSymbolic(mo, array, len);
 
  std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it =
    seedMap.find(&state);
  if (it == seedMap.end()) return;
 
  // In seed mode we need to add this as a binding.
  foreach (siit, it->second.begin(), it->second.end()) {
    if (!seedObject(state, *siit, mo, array))
      break;
  }
}


/***/

unsigned Executor::getSymbolicPathStreamID(const ExecutionState &state) {
  assert(symPathWriter);
  return state.symPathOS.getID();
}

void Executor::getConstraintLog(const ExecutionState &state,
                                std::string &res,
                                bool asCVC) {
  if (asCVC) {
    Query query(state.constraints, ConstantExpr::alloc(0, Expr::Bool));
    char *log = solver->stpSolver->getConstraintLog(query);
    res = std::string(log);
    free(log);
  } else {
    std::ostringstream info;
    ExprPPrinter::printConstraints(info, state.constraints);
    res = info.str();   
  }
}

void Executor::getSymbolicSolutionCex(
  const ExecutionState& state, ExecutionState& tmp)
{
  for (unsigned i = 0; i != state.symbolics.size(); ++i) {
    const MemoryObject *mo = state.symbolics[i].getMemoryObject();
    std::vector< ref<Expr> >::const_iterator pi =
      mo->cexPreferences.begin(), pie = mo->cexPreferences.end();
    for (; pi != pie; ++pi) {
      bool mustBeTrue, success;
     
      success = solver->mustBeTrue(tmp, Expr::createIsZero(*pi), mustBeTrue);
      if (!success) break;
      if (!mustBeTrue) tmp.addConstraint(*pi);
    }
    if (pi!=pie) break;
  }
}

bool Executor::getSymbolicSolution(
	const ExecutionState &state,
	std::vector<
		std::pair<std::string,
			std::vector<unsigned char> > > &res)
{
  ExecutionState tmp(state);
  if (!NoPreferCex) getSymbolicSolutionCex(state, tmp);

  std::vector< std::vector<unsigned char> > values;
  std::vector<const Array*> objects;
  for (unsigned i = 0; i != state.symbolics.size(); ++i)
    objects.push_back(state.symbolics[i].getArray());
  bool success = solver->getInitialValues(tmp, objects, values);
  if (!success) {
    klee_warning("unable to compute initial values (invalid constraints?)!");
    ExprPPrinter::printQuery(std::cerr,
                             state.constraints,
                             ConstantExpr::alloc(0, Expr::Bool));
    return false;
  }
 
  for (unsigned i = 0; i != state.symbolics.size(); ++i)
    res.push_back(
      std::make_pair(state.symbolics[i].getMemoryObject()->name, values[i]));
  return true;
}

void Executor::getCoveredLines(
  const ExecutionState &state,
  std::map<const std::string*, std::set<unsigned> > &res)
{
  res = state.coveredLines;
}

void Executor::doImpliedValueConcretization(
	ExecutionState &state,
	ref<Expr> e,
	ref<ConstantExpr> value)
{
  abort(); // FIXME: Broken until we sort out how to do the write back.

  if (DebugCheckForImpliedValues)
    ImpliedValue::checkForImpliedValues(solver->solver, e, value);

  ImpliedValueList results;
  ImpliedValue::getImpliedValues(e, value, results);

  foreach (it, results.begin(), results.end()) {
    ReadExpr *re = it->first.get();
    ConstantExpr *CE = dyn_cast<ConstantExpr>(re->index);
   
    if (!CE) continue;

    const MemoryObject *mo = 0; //re->updates.root->object;
    const ObjectState *os = state.addressSpace.findObject(mo);

    // os = 0 => obj has been free'd, no need to concretize (although as
    // in other cases we would like to concretize the outstanding
    // reads, but we have no facility for that yet)
    if (!os) continue;

    assert(!os->readOnly &&
           "not possible? read only object with static read?");
    ObjectState *wos = state.addressSpace.getWriteable(mo, os);
    //wos->write(CE, it->second);
    state.write(wos, CE, it->second);
  }
}

void Executor::initializeGlobalObject(
	ExecutionState &state,
	ObjectState *os,
	Constant *c,
 	unsigned offset)
{
	if (ConstantVector *cp = dyn_cast<ConstantVector>(c)) {
		unsigned elementSize;
		
		elementSize = target_data->getTypeStoreSize(
			cp->getType()->getElementType());
		for (unsigned i=0, e=cp->getNumOperands(); i != e; ++i)
			initializeGlobalObject(
				state,
				os,
				cp->getOperand(i),
				offset + i*elementSize);
	} else if (isa<ConstantAggregateZero>(c)) {
		unsigned size;
		size = target_data->getTypeStoreSize(c->getType());
		for (unsigned i=0; i<size; i++) {
			//os->write8(offset+i, (uint8_t) 0);
			state.write8(os, offset+i, (uint8_t) 0);
		}
	} else if (ConstantArray *ca = dyn_cast<ConstantArray>(c)) {
		unsigned elementSize;
		elementSize = target_data->getTypeStoreSize(
			ca->getType()->getElementType());
		for (unsigned i=0, e=ca->getNumOperands(); i != e; ++i)
			initializeGlobalObject(
				state,
				os,
				ca->getOperand(i),
				offset + i*elementSize);
	} else if (ConstantStruct *cs = dyn_cast<ConstantStruct>(c)) {
		const StructLayout *sl;
		sl = target_data->getStructLayout(
			cast<StructType>(cs->getType()));
		for (unsigned i=0, e=cs->getNumOperands(); i != e; ++i)
			initializeGlobalObject(
				state,
				os,
				cs->getOperand(i),
				offset + sl->getElementOffset(i));
	} else {
		unsigned StoreBits;
		ref<ConstantExpr> C;

		C = evalConstant(c);
		StoreBits = target_data->getTypeStoreSizeInBits(c->getType());

		// Extend the constant if necessary;
		assert(StoreBits >= C->getWidth() && "Invalid store size!");
		if (StoreBits > C->getWidth())
			C = C->ZExt(StoreBits);

		//os->write(offset, C);
		state.write(os, offset, C);
	}
}

Function* Executor::getCalledFunction(CallSite &cs, ExecutionState &state)
{
	Function *f;
	
	f = cs.getCalledFunction();

	if (!f) return f;
	std::string alias = state.getFnAlias(f->getName());
	if (alias == "") return f;

	llvm::Module* currModule = kmodule->module;
	Function* old_f = f;
	f = currModule->getFunction(alias);
	if (!f) {
		llvm::errs() << 
			"Function " << alias << "(), alias for " << 
			old_f->getName() << " not found!\n";
		assert(f && "function alias not found");
	}

	return f;
}

Expr::Width Executor::getWidthForLLVMType(const llvm::Type* type) const
{
	return kmodule->targetData->getTypeSizeInBits(type);
}

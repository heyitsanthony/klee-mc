//===-- Executor.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#include "Executor.h"
 
#include "Context.h"
#include "CoreStats.h"
#include "ExternalDispatcher.h"
#include "ImpliedValue.h"
#include "Memory.h"
#include "MemoryManager.h"
#include "StateRecord.h"
#include "PTree.h"
#include "Searcher.h"
#include "SeedInfo.h"
#include "SpecialFunctionHandler.h"
#include "StatsTracker.h"
#include "TimingSolver.h"
#include "UserSearcher.h"

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
#include "EquivalentStateEliminator.h"

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
#include "llvm/System/Process.h"
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
  UseEquivalentStateEliminator("use-equiv-state-elim",
                cl::init(false));
   
  cl::opt<bool>
  UseAsmAddresses("use-asm-addresses",
                  cl::init(false));
 
  cl::opt<bool>
  RandomizeFork("randomize-fork",
                cl::init(false));
 
  cl::opt<bool>
  AllowExternalSymCalls("allow-external-sym-calls",
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
  SuppressExternalWarnings("suppress-external-warnings");

  cl::opt<bool>
  AllExternalWarnings("all-external-warnings");

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
  NoExternals("no-externals", 
           cl::desc("Do not allow external functin calls"));

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
}


static void *theMMap = 0;
static unsigned theMMapSize = 0;

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
                             std::string stpQueryPCLogPath) {
  Solver *solver = stpSolver;

  if (UseSTPQueryPCLog)
    solver = createPCLoggingSolver(solver, 
                                   stpQueryPCLogPath);

  if (UseFastCexSolver)
    solver = createFastCexSolver(solver);

  if (UseCexCache)
    solver = createCexCachingSolver(solver);

  if (UseCache)
    solver = createCachingSolver(solver);

  if (UseIndependentSolver)
    solver = createIndependentSolver(solver);

  if (DebugValidateSolver)
    solver = createValidatingSolver(solver, stpSolver);

  if (UseQueryPCLog)
    solver = createPCLoggingSolver(solver, 
                                   queryPCLogPath);

  klee_message("BEGIN solver description");
  solver->printName();
  klee_message("END solver description");
  
  return solver;
}

namespace {
  struct KillOrCompactOrdering
  // the least important state (the first to kill or compact) first
  {
    // Ordering:
    // 1. States with coveredNew has greater importance
    // 2. States with more recent use has greater importance
    bool operator()(const ExecutionState* a, const ExecutionState* b) const
    // returns true if a is less important than b
    {
      if (!a->coveredNew &&  b->coveredNew) return true;
      if ( a->coveredNew && !b->coveredNew) return false;
      return a->lastChosen < b->lastChosen;
    }
  };
}

Executor::Executor(const InterpreterOptions &opts,
                   InterpreterHandler *ih) 
  : Interpreter(opts),
    equivStateElim(0),
    kmodule(0),
    interpreterHandler(ih),
    searcher(0),
    externalDispatcher(new ExternalDispatcher()),
    nonCompactStateCount(0),
    statsTracker(0),
    symPathWriter(0),
    specialFunctionHandler(0),
    processTree(0),
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

  memory = new MemoryManager();
}


const Module *Executor::setModule(llvm::Module *module, 
                                  const ModuleOptions &opts) {
  assert(!kmodule && module && "can only register one module"); // XXX gross
  
  kmodule = new KModule(module);

  // Initialize the context.
  TargetData *TD = kmodule->targetData;
  Context::initialize(TD->isLittleEndian(),
                      (Expr::Width) TD->getPointerSizeInBits());

  specialFunctionHandler = new SpecialFunctionHandler(*this);

  specialFunctionHandler->prepare();
  kmodule->prepare(opts, interpreterHandler);
  specialFunctionHandler->bind();

  if (StatsTracker::useStatistics()) {
    statsTracker = 
      new StatsTracker(*this,
                       interpreterHandler->getOutputFilename("assembly.ll"),
                       opts.ExcludeCovFiles,
                       userSearcherRequiresMD2U());
  }
  
  return module;
}

Executor::~Executor() {
  std::for_each(timers.begin(), timers.end(), deleteTimerInfo);
  delete memory;
  delete externalDispatcher;
  if (processTree)
    delete processTree;
  if (specialFunctionHandler)
    delete specialFunctionHandler;
  if (statsTracker)
    delete statsTracker;
  delete solver;
  delete kmodule;
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

inline void Executor::replaceState(ExecutionState* os, ExecutionState* ns)
{
  addedStates.insert(ns);
  removedStates.insert(os);
  replacedStates[os] = ns;
}

inline void Executor::replaceStateImmForked(ExecutionState* os, ExecutionState* ns)
{
//assert(removedStates.find(ns) == removedStates.end());
  /*thread->*/addedStates.insert(ns);
//assert(addedStates.find(os) != addedStates.end());
  /*thread->*/addedStates.erase(os);
  /*thread->*/replacedStates[os] = ns;
  removeStateInternal(os);
}
  
void Executor::initializeGlobalObject(ExecutionState &state, ObjectState *os,
                                      Constant *c, 
                                      unsigned offset) {
  TargetData *targetData = kmodule->targetData;
  if (ConstantVector *cp = dyn_cast<ConstantVector>(c)) {
    unsigned elementSize =
      targetData->getTypeStoreSize(cp->getType()->getElementType());
    for (unsigned i=0, e=cp->getNumOperands(); i != e; ++i)
      initializeGlobalObject(state, os, cp->getOperand(i), 
			     offset + i*elementSize);
  } else if (isa<ConstantAggregateZero>(c)) {
    unsigned i, size = targetData->getTypeStoreSize(c->getType());
    for (i=0; i<size; i++) {
      //os->write8(offset+i, (uint8_t) 0);
      state.write8(os, offset+i, (uint8_t) 0);
    }
  } else if (ConstantArray *ca = dyn_cast<ConstantArray>(c)) {
    unsigned elementSize =
      targetData->getTypeStoreSize(ca->getType()->getElementType());
    for (unsigned i=0, e=ca->getNumOperands(); i != e; ++i)
      initializeGlobalObject(state, os, ca->getOperand(i), 
			     offset + i*elementSize);
  } else if (ConstantStruct *cs = dyn_cast<ConstantStruct>(c)) {
    const StructLayout *sl =
      targetData->getStructLayout(cast<StructType>(cs->getType()));
    for (unsigned i=0, e=cs->getNumOperands(); i != e; ++i)
      initializeGlobalObject(state, os, cs->getOperand(i), 
			     offset + sl->getElementOffset(i));
  } else {
    unsigned StoreBits = targetData->getTypeStoreSizeInBits(c->getType());
    ref<ConstantExpr> C = evalConstant(c);

    // Extend the constant if necessary;
    assert(StoreBits >= C->getWidth() && "Invalid store size!");
    if (StoreBits > C->getWidth())
      C = C->ZExt(StoreBits);

    //os->write(offset, C);
    state.write(os, offset, C);
  }
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
  if (isReadOnly)
    os->setReadOnly(true);  
  return mo;
}

void Executor::initializeGlobals(ExecutionState &state) {
  Module *m = kmodule->module;

  if (m->getModuleInlineAsm() != "")
    klee_warning("executable has module level assembly (ignoring)");

  assert(m->lib_begin() == m->lib_end() &&
         "XXX do not support dependent libraries");

  // represent function globals using the address of the actual llvm function
  // object. given that we use malloc to allocate memory in states this also
  // ensures that we won't conflict. we don't need to allocate a memory object
  // since reading/writing via a function pointer is unsupported anyway.
  for (Module::iterator i = m->begin(), ie = m->end(); i != ie; ++i) {
    Function *f = i;
    ref<ConstantExpr> addr(0);

    // If the symbol has external weak linkage then it is implicitly
    // not defined in this module; if it isn't resolvable then it
    // should be null.
    if (f->hasExternalWeakLinkage() && 
        !externalDispatcher->resolveSymbol(f->getNameStr())) {
      addr = Expr::createPointer(0);
    } else {
      addr = Expr::createPointer((unsigned long) (void*) f);
      legalFunctions.insert((uint64_t) (unsigned long) (void*) f);
    }
    
    globalAddresses.insert(std::make_pair(f, addr));
  }

  // Disabled, we don't want to promote use of live externals.
#ifdef HAVE_CTYPE_EXTERNALS
#ifndef WINDOWS
#ifndef DARWIN
  /* From /usr/include/errno.h: it [errno] is a per-thread variable. */
  int *errno_addr = __errno_location();
  addExternalObject(state, (void *)errno_addr, sizeof *errno_addr, false);

  /* from /usr/include/ctype.h:
       These point into arrays of 384, so they can be indexed by any `unsigned
       char' value [0,255]; by EOF (-1); or by any `signed char' value
       [-128,-1).  ISO C requires that the ctype functions work for `unsigned */
  const uint16_t **addr = __ctype_b_loc();
  addExternalObject(state, (void *)(*addr-128), 
                    384 * sizeof **addr, true);
  addExternalObject(state, addr, sizeof(*addr), true);
    
  const int32_t **lower_addr = __ctype_tolower_loc();
  addExternalObject(state, (void *)(*lower_addr-128), 
                    384 * sizeof **lower_addr, true);
  addExternalObject(state, lower_addr, sizeof(*lower_addr), true);
  
  const int32_t **upper_addr = __ctype_toupper_loc();
  addExternalObject(state, (void *)(*upper_addr-128), 
                    384 * sizeof **upper_addr, true);
  addExternalObject(state, upper_addr, sizeof(*upper_addr), true);
#endif
#endif
#endif

  // allocate and initialize globals, done in two passes since we may
  // need address of a global in order to initialize some other one.

  // allocate memory objects for all globals
  for (Module::const_global_iterator i = m->global_begin(),
         e = m->global_end();
       i != e; ++i) {
    if (i->isDeclaration()) {
      // FIXME: We have no general way of handling unknown external
      // symbols. If we really cared about making external stuff work
      // better we could support user definition, or use the EXE style
      // hack where we check the object file information.

      const Type *ty = i->getType()->getElementType();
      uint64_t size = kmodule->targetData->getTypeStoreSize(ty);

      // XXX - DWD - hardcode some things until we decide how to fix.
#ifndef WINDOWS
      if (i->getName() == "_ZTVN10__cxxabiv117__class_type_infoE") {
        size = 0x2C;
      } else if (i->getName() == "_ZTVN10__cxxabiv120__si_class_type_infoE") {
        size = 0x2C;
      } else if (i->getName() == "_ZTVN10__cxxabiv121__vmi_class_type_infoE") {
        size = 0x2C;
      }
#endif

      if (size == 0) {
        llvm::errs() << "Unable to find size for global variable: " 
                     << i->getName() 
                     << " (use will result in out of bounds access)\n";
      }

      MemoryObject *mo = memory->allocate(size, false, true, i, &state);
      ObjectState *os = bindObjectInState(state, mo, false);
      globalObjects.insert(std::make_pair(i, mo));
      globalAddresses.insert(std::make_pair(i, mo->getBaseExpr()));

      // Program already running = object already initialized.  Read
      // concrete value and write it to our copy.
      if (size) {
        void *addr;
        if (i->getName() == "__dso_handle") {
          extern void *__dso_handle __attribute__ ((__weak__));
          addr = &__dso_handle; // wtf ?
        } else {
          addr = externalDispatcher->resolveSymbol(i->getNameStr());
        }
        if (!addr)
          klee_error("unable to load symbol(%s) while initializing globals.", 
                     i->getName().data());

        for (unsigned offset=0; offset<mo->size; offset++) {
          //os->write8(offset, ((unsigned char*)addr)[offset]);
          state.write8(os, offset, ((unsigned char*)addr)[offset]);
        }
      }
    } else {
      const Type *ty = i->getType()->getElementType();
      uint64_t size = kmodule->targetData->getTypeStoreSize(ty);
      MemoryObject *mo = 0;

      if (UseAsmAddresses && i->getName()[0]=='\01') {
        char *end;
        uint64_t address = ::strtoll(i->getNameStr().c_str()+1, &end, 0);

        if (end && *end == '\0') {
          // We can't use the PRIu64 macro here for some reason, so we have to
          // cast to long long unsigned int to avoid compiler warnings.
          klee_message("NOTE: allocated global at asm specified address: %#08llx"
                       " (%llu bytes)",
                       (long long unsigned int) address,
                       (long long unsigned int) size);
          mo = memory->allocateFixed(address, size, &*i, &state);
          mo->isUserSpecified = true; // XXX hack;
        }
      }

      if (!mo)
        mo = memory->allocate(size, false, true, &*i, &state);
      assert(mo && "out of memory");
      ObjectState *os = bindObjectInState(state, mo, false);
      globalObjects.insert(std::make_pair(i, mo));
      globalAddresses.insert(std::make_pair(i, mo->getBaseExpr()));

      if (!i->hasInitializer())
          os->initializeToRandom();
    }
  }
  
  // link aliases to their definitions (if bound)
  for (Module::alias_iterator i = m->alias_begin(), ie = m->alias_end(); 
       i != ie; ++i) {
    // Map the alias to its aliasee's address. This works because we have
    // addresses for everything, even undefined functions. 
    globalAddresses.insert(std::make_pair(i, evalConstant(i->getAliasee())));
  }

  // once all objects are allocated, do the actual initialization
  for (Module::const_global_iterator i = m->global_begin(),
         e = m->global_end();
       i != e; ++i) {
    if (i->hasInitializer()) {
      MemoryObject *mo = globalObjects.find(i)->second;
      const ObjectState *os = state.addressSpace.findObject(mo);
      assert(os);
      ObjectState *wos = state.addressSpace.getWriteable(mo, os);
      
      initializeGlobalObject(state, wos, i->getInitializer(), 0);
      // if (i->isConstant()) os->setReadOnly(true);
    }
  }
}

Executor::StatePair
Executor::fork(ExecutionState &current, ref<Expr> condition, bool isInternal) {
  ref<Expr> conditions[2];
  SeedMapType::iterator it = seedMap.find(&current);
  bool isSeeding = it != seedMap.end();

  // !!! is this the correct behavior?
  // Unlikely.
  if (!isSeeding &&
      !isa<ConstantExpr>(condition) &&
      (MaxStaticForkPct!=1. || MaxStaticSolvePct != 1. ||
       MaxStaticCPForkPct!=1. || MaxStaticCPSolvePct != 1.) &&
      statsTracker->elapsed() > 60.) {
    StatisticManager &sm = *theStatisticManager;
    CallPathNode *cpn = current.stack.back().callPathNode;
    if ((MaxStaticForkPct<1. &&
         sm.getIndexedValue(stats::forks, sm.getIndex()) > stats::forks*MaxStaticForkPct) ||
        (MaxStaticCPForkPct<1. &&
         cpn && (cpn->statistics.getValue(stats::forks) > stats::forks*MaxStaticCPForkPct)) ||
        (MaxStaticSolvePct<1 &&
         sm.getIndexedValue(stats::solverTime, sm.getIndex()) > stats::solverTime*MaxStaticSolvePct) ||
        (MaxStaticCPForkPct<1. &&
         cpn && (cpn->statistics.getValue(stats::solverTime) > stats::solverTime*MaxStaticCPSolvePct))) {
      ref<ConstantExpr> value;
      bool success = solver->getValue(current, condition, value);
      assert(success && "FIXME: Unhandled solver failure");
      addConstraint(current, EqExpr::create(value, condition));
      condition = value;
    }
  }

//  conditions[0] = Expr::createIsZero(condition);
  conditions[1] = condition;

  StateVector results = fork(current, 2, conditions, isInternal, true);
  return std::make_pair(results[1], results[0]);
}

// !!! for normal branch, conditions = {false,true} so that replay 0,1 reflects
// index
Executor::StateVector
Executor::fork(ExecutionState &current,
               unsigned N, ref<Expr> conditions[],
               bool isInternal, bool isBranch) {
  std::vector<bool> res(N, false);
  StateVector resStates(N, NULL);
  SeedMapType::iterator it = seedMap.find(&current);
  bool isSeeding = it != seedMap.end();
  std::vector<std::list<SeedInfo> > resSeeds(N);

  // Evaluate fork conditions
  double timeout = stpTimeout;
  if (isSeeding)
    timeout *= it->second.size();
  unsigned condIndex, validTargets = 0, feasibleTargets = 0;

  if (isBranch) {
    Solver::Validity result;
    solver->setTimeout(timeout);
    bool success = solver->evaluate(current, conditions[1], result);
    solver->setTimeout(0);
    if (!success) {
      terminateStateEarly(current, "query timed out");
      return StateVector(N, NULL);
    }
    res[0] = (result == Solver::False || result == Solver::Unknown);
    res[1] = (result == Solver::True || result == Solver::Unknown);
    validTargets = (result == Solver::Unknown ? 2 : 1);
    if (validTargets > 1 || isSeeding)
      conditions[0] = Expr::createIsZero(conditions[1]);
  }
  else {
    for(condIndex = 0; condIndex < N; condIndex++) {
      bool result;
      // If condition is a constant (e.g., from constant switch statement), don't
      // generate a query
      if (ConstantExpr *CE
          = dyn_cast<ConstantExpr>(conditions[condIndex])) {
        if (CE->isFalse())
          result = false;
        else if (CE->isTrue()) {
          result = true;
        }
        else
          assert(false && "Invalid constant fork condition");
      }
      else {
        solver->setTimeout(timeout);
        bool success = solver->mayBeTrue(current, conditions[condIndex], result);
        solver->setTimeout(0);
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
    if (!isInternal && current.isCompactForm) {
      // Can't fork compact states; sanity check
      assert(false && "invalid state");
    } else if (!isInternal && ReplayPathOnly && current.isReplay
      && current.replayBranchIterator == current.branchDecisionsSequence.end())
    {
      // Done replaying this state, so kill it (if -replay-path-only)
      terminateStateEarly(current, "replay path exhausted");
      return StateVector(N, NULL);
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
          if (res[i]) {
            if (!first)
              ss << ",";
            ss << i;
            first = false;
          }
        }
        ss << ")";
        terminateStateOnError(current, ss.str().c_str(), "branch.err");
        klee_warning("hit invalid branch in replay path mode");
        return StateVector(N, NULL);
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

      if (!isInternal) {
        const char* reason = 0;
        if (MaxMemoryInhibit && atMemoryLimit)
          reason = "memory cap exceeded";
        if (current.forkDisabled)
          reason = "fork disabled on current path";
        if (inhibitForking)
          reason = "fork disabled globally";
        if (MaxForks!=~0u && stats::forks >= MaxForks)
          reason = "max-forks reached";

        // Skipping this fork for one of the above reasons; randomly pick target
        if (reason) {
          if (!ReplayInhibitedForks) {
            klee_warning_once(reason, "skipping fork and pruning randomly (%s)",
              reason);
            TimerStatIncrementer timer(stats::forkTime);
            unsigned randIndex = (theRNG.getInt32() % validTargets) + 1;
            for(condIndex = 0; condIndex < N; condIndex++) {
              if (res[condIndex])
                randIndex--;
              if (!randIndex)
                break;
            }
            assert(condIndex < N);
            validTargets = 1;
            res.assign(N, false);
            res[condIndex] = true;
          }
          else {
            klee_warning_once(reason, "forking into compact forms (%s)",
              reason);
            forkCompact = true;
          }
        } // if (reason)
      } // if (!isInternal)
    } // if (validTargets > 1)
  } // if (!isSeeding)

  // Fix branch in only-replay-seed mode, if we don't have both true
  // and false seeds.

  else if (isSeeding)
  {
    // Assume each seed only satisfies one condition (necessarily true
    // when conditions are mutually exclusive and their conjunction is
    // a tautology).
    // This partitions the seed set for the current state
    for (std::vector<SeedInfo>::iterator siit = it->second.begin(), 
           siie = it->second.end(); siit != siie; ++siit) {
      unsigned i;
      for (i = 0; i < N; ++i) {
        ref<ConstantExpr> res;
        bool success = solver->getValue(current,
          siit->assignment.evaluate(conditions[i]), res);
        assert(success && "FIXME: Unhandled solver failure");
        if (res->isTrue())
          break;
      }

      // If we didn't find a satisfying condition, randomly pick one
      // (the seed will be patched).
      if (i == N)
        i = theRNG.getInt32() % N;

      resSeeds[i].push_back(*siit);
    }

    // Clear any valid conditions that seeding rejects
    if ((current.forkDisabled || OnlyReplaySeeds) && validTargets > 1) {
      validTargets = 0;
      for (unsigned i = 0; i < N; i++) {
        if (resSeeds[i].empty())
          res[i] = false;
        if (res[i])
          validTargets++;
      }
      assert(validTargets && "seed must result in at least one valid target");
    }

    // Remove seeds corresponding to current state
    seedMap.erase(it);
    
    // !!! it's possible for the current state to end up with no seeds. Does
    // this matter? Old fork() used to handle it but branch() didn't.
  }

  bool curStateUsed = false;
  // Loop for actually forking states
  for(condIndex = 0; condIndex < N; condIndex++) {
    ExecutionState *baseState = &current;
    // Process each valid target and generate a state
    if (res[condIndex]) {
      ExecutionState *newState;
      if (!curStateUsed) {
        resStates[condIndex] = baseState;
        curStateUsed = true;
      }
      else {
        assert(!forkCompact || ReplayInhibitedForks);

        // Update stats
        TimerStatIncrementer timer(stats::forkTime);
        ++stats::forks;
        
        // Do actual state forking
        newState = forkCompact ? current.branchForReplay()
                               : current.branch();
        /*thread->*/addedStates.insert(newState);
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
    } // if (res[valIndex])
  } // for

  // Loop for bookkeeping (loops must be separate since states are forked from
  // each other)
  for(condIndex = 0; condIndex < N; condIndex++) {
    if (res[condIndex]) {
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
            curState->branchDecisionsSequence.end()) {
          curState->branchDecisionsSequence.push_back(condIndex,
            current.prevPC->info->assemblyLine);
          curState->replayBranchIterator =
            curState->branchDecisionsSequence.end();
        }
      } // if (!isInternal)

      if (isSeeding) {
        seedMap[curState].insert(seedMap[curState].end(),
          resSeeds[condIndex].begin(), resSeeds[condIndex].end());
      }

    } // if (res[valIndex])
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

ref<klee::ConstantExpr> Executor::evalConstant(Constant *c) {
  if (llvm::ConstantExpr *ce = dyn_cast<llvm::ConstantExpr>(c)) {
    return evalConstantExpr(ce);
  } else {
    if (const ConstantInt *ci = dyn_cast<ConstantInt>(c)) {
      return ConstantExpr::alloc(ci->getValue());
    } else if (const ConstantFP *cf = dyn_cast<ConstantFP>(c)) {      
      return ConstantExpr::alloc(cf->getValueAPF().bitcastToAPInt());
    } else if (const GlobalValue *gv = dyn_cast<GlobalValue>(c)) {
      return globalAddresses.find(gv)->second;
    } else if (isa<ConstantPointerNull>(c)) {
      return Expr::createPointer(0);
    } else if (isa<UndefValue>(c)) {
      return ConstantExpr::create(0, Expr::getWidthForLLVMType(c->getType()));
    } else {
      // Constant{AggregateZero,Array,Struct,Vector}
      assert(0 && "invalid argument to evalConstant()");
    }
  }
}

const Cell& Executor::eval(KInstruction *ki, unsigned index, 
                           ExecutionState &state) const {
  assert(index < ki->inst->getNumOperands());
  int vnumber = ki->operands[index];

  assert(vnumber != -1 &&
         "Invalid operand to eval(), not a value or constant!");

  // Determine if this is a constant or not.
  if (vnumber < 0) {
    unsigned index = -vnumber - 2;
    return kmodule->constantTable[index];
  } else {
    unsigned index = vnumber;
    //StackFrame &sf = state.stack.back();
    //return sf.locals[index];
    return state.readLocalCell(state.stack.size() - 1, index);
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

    solver->setTimeout(stpTimeout);      
    if (solver->getValue(state, e, value) &&
        solver->mustBeTrue(state, EqExpr::create(e, value), isTrue) &&
        isTrue)
      result = value;
    solver->setTimeout(0);
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

void Executor::executeGetValue(ExecutionState &state,
                               ref<Expr> e,
                               KInstruction *target) {
  e = state.constraints.simplifyExpr(e);
  std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it = 
    seedMap.find(&state);
  if (it==seedMap.end() || isa<ConstantExpr>(e)) {
    ref<ConstantExpr> value;
    bool success = solver->getValue(state, e, value);
    assert(success && "FIXME: Unhandled solver failure");
    (void) success;
    bindLocal(target, state, value);
  } else {
    std::set< ref<Expr> > values;
    for (std::vector<SeedInfo>::iterator siit = it->second.begin(), 
           siie = it->second.end(); siit != siie; ++siit) {
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
      if (es)
        bindLocal(target, *es, *vit);
      ++bit;
    }
  }
}

void Executor::stepInstruction(ExecutionState &state) {
  if (DebugPrintInstructions) {
    printFileLine(state, state.pc);
    std::cerr << std::setw(10) << stats::instructions << " "
              << *(state.pc->inst) << "\n";
  }

  if (statsTracker)
    statsTracker->stepInstruction(state);

  ++stats::instructions;
  state.prevPC = state.pc;
  ++state.pc;

  if (stats::instructions==StopAfterNInstructions)
    haltExecution = true;
}

void Executor::executeCall(ExecutionState &state, 
                           KInstruction *ki,
                           Function *f,
                           std::vector< ref<Expr> > &arguments) {
  if (WriteTraces)
    state.exeTraceMgr.addEvent(new FunctionCallTraceEvent(state, ki,
                                                          f->getName()));

  Instruction *i = ki->inst;
  if (f && f->isDeclaration()) {
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
  } else {
    // FIXME: I'm not really happy about this reliance on prevPC but it is ok, I
    // guess. This just done to avoid having to pass KInstIterator everywhere
    // instead of the actual instruction, since we can't make a KInstIterator
    // from just an instruction (unlike LLVM).
    KFunction *kf = kmodule->functionMap[f];
    state.pushFrame(state.prevPC, kf);
    state.pc = kf->instructions;
        
    if (statsTracker)
      statsTracker->framePushed(state, &state.stack[state.stack.size()-2]);
 
     // TODO: support "byval" parameter attribute
     // TODO: support zeroext, signext, sret attributes
        
    unsigned callingArgs = arguments.size();
    unsigned funcArgs = f->arg_size();
    if (!f->isVarArg()) {
      if (callingArgs > funcArgs) {
        klee_warning_once(f, "calling %s with extra arguments.", 
                          f->getName().data());
      } else if (callingArgs < funcArgs) {
        terminateStateOnError(state, "calling function with too few arguments", 
                              "user.err");
        return;
      }
    } else {
      if (callingArgs < funcArgs) {
        terminateStateOnError(state, "calling function with too few arguments", 
                              "user.err");
        return;
      }
            
      StackFrame &sf = state.stack.back();
      unsigned size = 0;
      for (unsigned i = funcArgs; i < callingArgs; i++) {
        // FIXME: This is really specific to the architecture, not the pointer
        // size. This happens to work fir x86-32 and x86-64, however.
        Expr::Width WordSize = Context::get().getPointerWidth();
        if (WordSize == Expr::Int32) {
          size += Expr::getMinBytesForWidth(arguments[i]->getWidth());
        } else {
          size += llvm::RoundUpToAlignment(arguments[i]->getWidth(), 
                                           WordSize) / 8;
        }
      }

      MemoryObject *mo = sf.varargs = memory->allocate(size, true, false, 
                                                       state.prevPC->inst,
                                                       &state);
      if (!mo) {
        terminateStateOnExecError(state, "out of memory (varargs)");
        return;
      }
      ObjectState *os = bindObjectInState(state, mo, true);
      unsigned offset = 0;
      for (unsigned i = funcArgs; i < callingArgs; i++) {
        // FIXME: This is really specific to the architecture, not the pointer
        // size. This happens to work fir x86-32 and x86-64, however.
        Expr::Width WordSize = Context::get().getPointerWidth();
        if (WordSize == Expr::Int32) {
          //os->write(offset, arguments[i]);
          state.write(os, offset, arguments[i]);
          offset += Expr::getMinBytesForWidth(arguments[i]->getWidth());
        } else {
          assert(WordSize == Expr::Int64 && "Unknown word size!");
          //os->write(offset, arguments[i]);
          state.write(os, offset, arguments[i]);
          offset += llvm::RoundUpToAlignment(arguments[i]->getWidth(), 
                                             WordSize) / 8;
        }
      }
    }

    unsigned numFormals = f->arg_size();
    for (unsigned i=0; i<numFormals; ++i) 
      bindArgument(kf, i, state, arguments[i]);
  }
}

void Executor::transferToBasicBlock(BasicBlock *dst, BasicBlock *src, 
                                    ExecutionState &state) {
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


Function* Executor::getCalledFunction(CallSite &cs, ExecutionState &state) {
  Function *f = cs.getCalledFunction();
  
  if (f) {
    std::string alias = state.getFnAlias(f->getName());
    if (alias != "") {
      llvm::Module* currModule = kmodule->module;
      Function* old_f = f;
      f = currModule->getFunction(alias);
      if (!f) {
	llvm::errs() << "Function " << alias << "(), alias for " 
                     << old_f->getName() << " not found!\n";
	assert(f && "function alias not found");
      }
    }
  }
  
  return f;
}

static bool isDebugIntrinsic(const Function *f, KModule *KM) {
  // Fast path, getIntrinsicID is slow.
  if (f == KM->dbgStopPointFn)
    return true;

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

void Executor::executeInstruction(ExecutionState &state, KInstruction *ki) {
  Instruction *i = ki->inst;
  switch (i->getOpcode()) {
    // Control flow
  case Instruction::Ret: {
    ReturnInst *ri = cast<ReturnInst>(i);
    KInstIterator kcaller = state.stack.back().caller;
    Instruction *caller = kcaller ? kcaller->inst : 0;
    bool isVoidReturn = (ri->getNumOperands() == 0);
    ref<Expr> result = ConstantExpr::alloc(0, Expr::Bool);

    if (WriteTraces) {
      state.exeTraceMgr.addEvent(new FunctionReturnTraceEvent(state, ki));
    }
    
    if (!isVoidReturn) {
      result = eval(ki, 0, state).value;
    }
    
    if (state.stack.size() <= 1) {
      assert(!caller && "caller set on initial stack frame");
      terminateStateOnExit(state);
    } else {
      state.popFrame();

      if (statsTracker)
        statsTracker->framePopped(state);

      if (InvokeInst *ii = dyn_cast<InvokeInst>(caller)) {
        transferToBasicBlock(ii->getNormalDest(), caller->getParent(), state);
      } else {
        state.pc = kcaller;
        ++state.pc;
      }

      if (!isVoidReturn) {
        const Type *t = caller->getType();
        if (t != Type::getVoidTy(getGlobalContext())) {
          // may need to do coercion due to bitcasts
          Expr::Width from = result->getWidth();
          Expr::Width to = Expr::getWidthForLLVMType(t);
            
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
      } else {
        // We check that the return value has no users instead of
        // checking the type, since C defaults to returning int for
        // undeclared functions.
        if (!caller->use_empty()) {
          terminateStateOnExecError(state, "return void when caller expected a result");
        }
      }
    }      
    break;
  }
  case Instruction::Unwind: {
    for (;;) {
      KInstruction *kcaller = state.stack.back().caller;
      state.popFrame();

      if (statsTracker)
        statsTracker->framePopped(state);

      if (state.stack.empty()) {
        terminateStateOnExecError(state, "unwind from initial stack frame");
        break;
      } else {
        Instruction *caller = kcaller->inst;
        if (InvokeInst *ii = dyn_cast<InvokeInst>(caller)) {
          transferToBasicBlock(ii->getUnwindDest(), caller->getParent(), state);
          break;
        }
      }
    }
    break;
  }
  case Instruction::Br: {
    BranchInst *bi = cast<BranchInst>(i);
    if (bi->isUnconditional()) {
      transferToBasicBlock(bi->getSuccessor(0), bi->getParent(), state);
    } else {
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
    break;
  }
  case Instruction::Switch: {
    SwitchInst *si = cast<SwitchInst>(i);
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
      if (es) {
        BasicBlock *destBlock = caseDests[index];
        KFunction *kf = state.stack.back().kf;
        unsigned entry = kf->basicBlockEntry[destBlock];

        if (es->isCompactForm && kf->trackCoverage &&
            theStatisticManager->getIndexedValue(stats::uncoveredInstructions,
            kf->instructions[entry]->info->id)) {
          ExecutionState *newState = es->reconstitute(*initialStateCopy);
//          ExecutorThread *thread = ExecutorThread::getThread();
          replaceStateImmForked(es, newState);
          es = newState;
        }

        if (!es->isCompactForm)
          transferToBasicBlock(destBlock, bb, *es);
        
        // Update coverage stats
        if (kf->trackCoverage &&
            theStatisticManager->getIndexedValue(stats::uncoveredInstructions,
            kf->instructions[entry]->info->id)) {
          es->coveredNew = true;
          es->instsSinceCovNew = 1;
        }
      }
    }
    break;
 }
  case Instruction::Unreachable:
    // Note that this is not necessarily an internal bug, llvm will
    // generate unreachable instructions in cases where it knows the
    // program will crash. So it is effectively a SEGV or internal
    // error.
    terminateStateOnExecError(state, "reached \"unreachable\" instruction");
    break;

  case Instruction::Invoke:
  case Instruction::Call: {
    CallSite cs;
    unsigned argStart;
    if (i->getOpcode()==Instruction::Call) {
      cs = CallSite(cast<CallInst>(i));
      argStart = 1;
    } else {
      cs = CallSite(cast<InvokeInst>(i));
      argStart = 3;
    }

    unsigned numArgs = cs.arg_size();
    Function *f = getCalledFunction(cs, state);

    // Skip debug intrinsics, we can't evaluate their metadata arguments.
    if (f && isDebugIntrinsic(f, kmodule))
      break;

    // evaluate arguments
    std::vector< ref<Expr> > arguments;
    arguments.reserve(numArgs);

    for (unsigned j=0; j<numArgs; ++j)
      arguments.push_back(eval(ki, argStart+j, state).value);

    if (!f) {
      // special case the call with a bitcast case
      Value *fp = cs.getCalledValue();
      llvm::ConstantExpr *ce = dyn_cast<llvm::ConstantExpr>(fp);
        
      if (ce && ce->getOpcode()==Instruction::BitCast) {
        f = dyn_cast<Function>(ce->getOperand(0));
        assert(f && "XXX unrecognized constant expression in call");
        const FunctionType *fType = 
          dyn_cast<FunctionType>(cast<PointerType>(f->getType())->getElementType());
        const FunctionType *ceType =
          dyn_cast<FunctionType>(cast<PointerType>(ce->getType())->getElementType());
        assert(fType && ceType && "unable to get function type");

        // XXX check result coercion

        // XXX this really needs thought and validation
        unsigned i=0;
        for (std::vector< ref<Expr> >::iterator
               ai = arguments.begin(), ie = arguments.end();
             ai != ie; ++ai) {
          Expr::Width to, from = (*ai)->getWidth();
            
          if (i<fType->getNumParams()) {
            to = Expr::getWidthForLLVMType(fType->getParamType(i));

            if (from != to) {
              // XXX need to check other param attrs ?
              if (cs.paramHasAttr(i+1, llvm::Attribute::SExt)) {
                arguments[i] = SExtExpr::create(arguments[i], to);
              } else {
                arguments[i] = ZExtExpr::create(arguments[i], to);
              }
            }
          }
            
          i++;
        }
      } else if (isa<InlineAsm>(fp)) {
        terminateStateOnExecError(state, "inline assembly is unsupported");
        break;
      }
    }

    if (f) {
      executeCall(state, ki, f, arguments);
    } else {
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
            f = (Function*) addr;

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
    break;
  }
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

  case Instruction::Add: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    bindLocal(ki, state, AddExpr::create(left, right));
    break;
  }

  case Instruction::Sub: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    bindLocal(ki, state, SubExpr::create(left, right));
    break;
  }
 
  case Instruction::Mul: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    bindLocal(ki, state, MulExpr::create(left, right));
    break;
  }

  case Instruction::UDiv: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = UDivExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::SDiv: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = SDivExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::URem: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = URemExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }
 
  case Instruction::SRem: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = SRemExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::And: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = AndExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::Or: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = OrExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::Xor: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = XorExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::Shl: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = ShlExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::LShr: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = LShrExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::AShr: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = AShrExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

    // Compare

  case Instruction::ICmp: {
    CmpInst *ci = cast<CmpInst>(i);
    ICmpInst *ii = cast<ICmpInst>(ci);
 
    switch(ii->getPredicate()) {
    case ICmpInst::ICMP_EQ: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = EqExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_NE: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = NeExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_UGT: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = UgtExpr::create(left, right);
      bindLocal(ki, state,result);
      break;
    }

    case ICmpInst::ICMP_UGE: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = UgeExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_ULT: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = UltExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_ULE: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = UleExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_SGT: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = SgtExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_SGE: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = SgeExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_SLT: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = SltExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_SLE: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = SleExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    default:
      terminateStateOnExecError(state, "invalid ICmp predicate");
    }
    break;
  }
 
    // Memory instructions...
  case Instruction::Alloca:
  case Instruction::Malloc: {
    AllocationInst *ai = cast<AllocationInst>(i);
    unsigned elementSize = 
      kmodule->targetData->getTypeStoreSize(ai->getAllocatedType());
    ref<Expr> size = Expr::createPointer(elementSize);
    if (ai->isArrayAllocation()) {
      ref<Expr> count = eval(ki, 0, state).value;
      count = Expr::createCoerceToPointerType(count);
      size = MulExpr::create(size, count);
    }
    bool isLocal = i->getOpcode()==Instruction::Alloca;
    executeAlloc(state, size, isLocal, ki);
    break;
  }
  case Instruction::Free: {
    executeFree(state, eval(ki, 0, state).value);
    break;
  }

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

    for (std::vector< std::pair<unsigned, uint64_t> >::iterator 
           it = kgepi->indices.begin(), ie = kgepi->indices.end(); 
         it != ie; ++it) {
      uint64_t elementSize = it->second;
      ref<Expr> index = eval(ki, it->first, state).value;
      base = AddExpr::create(base,
                             MulExpr::create(Expr::createCoerceToPointerType(index),
                                             Expr::createPointer(elementSize)));
    }
    if (kgepi->offset)
      base = AddExpr::create(base,
                             Expr::createPointer(kgepi->offset));
    bindLocal(ki, state, base);
    break;
  }

    // Conversion
  case Instruction::Trunc: {
    CastInst *ci = cast<CastInst>(i);
    ref<Expr> result = ExtractExpr::create(eval(ki, 0, state).value,
                                           0,
                                           Expr::getWidthForLLVMType(ci->getType()));
    bindLocal(ki, state, result);
    break;
  }
  case Instruction::ZExt: {
    CastInst *ci = cast<CastInst>(i);
    ref<Expr> result = ZExtExpr::create(eval(ki, 0, state).value,
                                        Expr::getWidthForLLVMType(ci->getType()));
    bindLocal(ki, state, result);
    break;
  }
  case Instruction::SExt: {
    CastInst *ci = cast<CastInst>(i);
    ref<Expr> result = SExtExpr::create(eval(ki, 0, state).value,
                                        Expr::getWidthForLLVMType(ci->getType()));
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::IntToPtr: {
    CastInst *ci = cast<CastInst>(i);
    Expr::Width pType = Expr::getWidthForLLVMType(ci->getType());
    ref<Expr> arg = eval(ki, 0, state).value;
    bindLocal(ki, state, ZExtExpr::create(arg, pType));
    break;
  } 
  case Instruction::PtrToInt: {
    CastInst *ci = cast<CastInst>(i);
    Expr::Width iType = Expr::getWidthForLLVMType(ci->getType());
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
    Expr::Width resultType = Expr::getWidthForLLVMType(fi->getType());
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
    Expr::Width resultType = Expr::getWidthForLLVMType(fi->getType());
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
    Expr::Width resultType = Expr::getWidthForLLVMType(fi->getType());
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
    Expr::Width resultType = Expr::getWidthForLLVMType(fi->getType());
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
    Expr::Width resultType = Expr::getWidthForLLVMType(fi->getType());
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
    Expr::Width resultType = Expr::getWidthForLLVMType(fi->getType());
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

    bool Result = false;
    switch( fi->getPredicate() ) {
      // Predicates which only care about whether or not the operands are NaNs.
    case FCmpInst::FCMP_ORD:
      Result = CmpRes != APFloat::cmpUnordered;
      break;

    case FCmpInst::FCMP_UNO:
      Result = CmpRes == APFloat::cmpUnordered;
      break;

      // Ordered comparisons return false if either operand is NaN.  Unordered
      // comparisons return true if either operand is NaN.
    case FCmpInst::FCMP_UEQ:
      if (CmpRes == APFloat::cmpUnordered) {
        Result = true;
        break;
      }
    case FCmpInst::FCMP_OEQ:
      Result = CmpRes == APFloat::cmpEqual;
      break;

    case FCmpInst::FCMP_UGT:
      if (CmpRes == APFloat::cmpUnordered) {
        Result = true;
        break;
      }
    case FCmpInst::FCMP_OGT:
      Result = CmpRes == APFloat::cmpGreaterThan;
      break;

    case FCmpInst::FCMP_UGE:
      if (CmpRes == APFloat::cmpUnordered) {
        Result = true;
        break;
      }
    case FCmpInst::FCMP_OGE:
      Result = CmpRes == APFloat::cmpGreaterThan || CmpRes == APFloat::cmpEqual;
      break;

    case FCmpInst::FCMP_ULT:
      if (CmpRes == APFloat::cmpUnordered) {
        Result = true;
        break;
      }
    case FCmpInst::FCMP_OLT:
      Result = CmpRes == APFloat::cmpLessThan;
      break;

    case FCmpInst::FCMP_ULE:
      if (CmpRes == APFloat::cmpUnordered) {
        Result = true;
        break;
      }
    case FCmpInst::FCMP_OLE:
      Result = CmpRes == APFloat::cmpLessThan || CmpRes == APFloat::cmpEqual;
      break;

    case FCmpInst::FCMP_UNE:
      Result = CmpRes == APFloat::cmpUnordered || CmpRes != APFloat::cmpEqual;
      break;
    case FCmpInst::FCMP_ONE:
      Result = CmpRes != APFloat::cmpUnordered && CmpRes != APFloat::cmpEqual;
      break;

    default:
      assert(0 && "Invalid FCMP predicate!");
    case FCmpInst::FCMP_FALSE:
      Result = false;
      break;
    case FCmpInst::FCMP_TRUE:
      Result = true;
      break;
    }

    bindLocal(ki, state, ConstantExpr::alloc(Result, Expr::Bool));
    break;
  }
 
    // Other instructions...
    // Unhandled
  case Instruction::ExtractElement:
  case Instruction::InsertElement:
  case Instruction::ShuffleVector:
    terminateStateOnError(state, "XXX vector instructions unhandled",
                          "xxx.err");
    break;
 
  default:
    terminateStateOnExecError(state, "illegal instruction");
    break;
  }
}

void Executor::removeStateInternal(ExecutionState* es, ExecutionState** root_to_be_removed) {
  std::map<ExecutionState*, std::vector<SeedInfo> >::iterator it3 =
    seedMap.find(es);
  if (it3 != seedMap.end())
    seedMap.erase(it3);

  if (es->ptreeNode == processTree->root) {
    assert(root_to_be_removed);
    *root_to_be_removed = es;
  }
  else {
    assert(es->ptreeNode->data == es);
    std::map<ExecutionState*, ExecutionState*>::iterator it4 =
      replacedStates.find(es);
    if (it4 == replacedStates.end())
      processTree->remove(es->ptreeNode);
    else {
      ExecutionState* ns = it4->second;
      // replace the placeholder state in the process tree
      ns->ptreeNode = es->ptreeNode;
      ns->ptreeNode->data = ns;

      processTree->update(ns->ptreeNode, PTree::WeightCompact,
        !ns->isCompactForm);
    //processTree->update(ns->ptreeNode, PTree::WeightRunning,
    //  !ns->isRunning);
    }
    delete es;
  }
}

void Executor::updateStates(ExecutionState *current) {
  if (searcher) {
    searcher->update(current, addedStates, removedStates, ignoreStates, unignoreStates);
    ignoreStates.clear();
    unignoreStates.clear();
  }
  if (equivStateElim) {      
    equivStateElim->update(current, addedStates, removedStates, ignoreStates, unignoreStates);      
  }

  states.insert(addedStates.begin(), addedStates.end());
  nonCompactStateCount += std::count_if(/*thread->*/addedStates.begin(),
    /*thread->*/addedStates.end(),
    std::mem_fun(&ExecutionState::isNonCompactForm_f));
  addedStates.clear();

  ExecutionState* root_to_be_removed = 0;
  for (std::set<ExecutionState*>::iterator it = removedStates.begin();
       it != removedStates.end(); ++it) {
    ExecutionState *es = *it;

    std::set<ExecutionState*>::iterator it2 = states.find(es);
    assert(it2!=states.end());
    states.erase(it2);

    // this deref must happen before delete
    if (!es->isCompactForm) --nonCompactStateCount;

    removeStateInternal(es, &root_to_be_removed);
  }
  if (ExecutionState* es = root_to_be_removed) {
    std::map<ExecutionState*, ExecutionState*>::iterator it4 =
      replacedStates.find(es);
    if (it4 == replacedStates.end()) {
      delete processTree->root->data;
      processTree->root->data = 0;
    }
    else {
      ExecutionState* ns = it4->second;
      // replace the placeholder state in the process tree
      ns->ptreeNode = es->ptreeNode;
      ns->ptreeNode->data = ns;

      processTree->update(ns->ptreeNode, PTree::WeightCompact,
        !ns->isCompactForm);
//      processTree->update(ns->ptreeNode, PTree::WeightRunning,
//        !ns->isRunning);

      delete es;
    }
  }
  removedStates.clear();
  replacedStates.clear();

  if (nonCompactStateCount == 0 && !states.empty())
    onlyNonCompact = false;
}

void Executor::bindInstructionConstants(KInstruction *KI) {
  GetElementPtrInst *gepi = dyn_cast<GetElementPtrInst>(KI->inst);
  if (!gepi)
    return;

  KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(KI);
  ref<ConstantExpr> constantOffset =
    ConstantExpr::alloc(0, Context::get().getPointerWidth());
  uint64_t index = 1;
  for (gep_type_iterator ii = gep_type_begin(gepi), ie = gep_type_end(gepi);
       ii != ie; ++ii) {
    if (const StructType *st = dyn_cast<StructType>(*ii)) {
      const StructLayout *sl = kmodule->targetData->getStructLayout(st);
      const ConstantInt *ci = cast<ConstantInt>(ii.getOperand());
      uint64_t addend = sl->getElementOffset((unsigned) ci->getZExtValue());
      constantOffset = constantOffset->Add(ConstantExpr::alloc(addend,
                                                               Context::get().getPointerWidth()));
    } else {
      const SequentialType *st = cast<SequentialType>(*ii);
      uint64_t elementSize = 
        kmodule->targetData->getTypeStoreSize(st->getElementType());
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

void Executor::bindModuleConstants() {
  for (std::vector<KFunction*>::iterator it = kmodule->functions.begin(), 
         ie = kmodule->functions.end(); it != ie; ++it) {
    KFunction *kf = *it;
    for (unsigned i=0; i<kf->numInstructions; ++i)
      bindInstructionConstants(kf->instructions[i]);
  }

  kmodule->constantTable = new Cell[kmodule->constants.size()];
  for (unsigned i=0; i<kmodule->constants.size(); ++i) {
    Cell &c = kmodule->constantTable[i];
    c.value = evalConstant(kmodule->constants[i]);
  }
}

std::set<Function*> funcs;

void Executor::run(ExecutionState &initialState) {
  bindModuleConstants();

  // Delay init till now so that ticks don't accrue during
  // optimization and such.
  initTimers();

  if (ReplayInhibitedForks) {
    initialStateCopy = new ExecutionState(initialState);
  }

  if(replayPaths) {
    for(std::list<ReplayPathType>::const_iterator it = replayPaths->begin();
        it != replayPaths->end(); ++it) {
      ExecutionState *newState = new ExecutionState(initialState);
      for(ReplayPathType::const_iterator it2 = (*it).begin();
          it2 != (*it).end(); ++it2) {
        newState->branchDecisionsSequence.push_back(*it2);
      }
      newState->replayBranchIterator =
        newState->branchDecisionsSequence.begin();
      newState->ptreeNode->data = 0;
      newState->isReplay = true;
      splitProcessTree(newState->ptreeNode, &initialState, newState);
      /*ExecutorThread::getThread()->*/addedStates.insert(newState);
      ++nonCompactStateCount;
    }
    // remove initial state from ptree
    states.insert(&initialState);
    /*ExecutorThread::getThread()->*/removedStates.insert(&initialState);
    updateStates(NULL/*, ExecutorThread::getThread()*/);
  } else {
    states.insert(&initialState);
    ++nonCompactStateCount;
  }

  if (usingSeeds) {
    std::vector<SeedInfo> &v = seedMap[&initialState];
    
    for (std::vector<KTest*>::const_iterator it = usingSeeds->begin(), 
           ie = usingSeeds->end(); it != ie; ++it)
      v.push_back(SeedInfo(*it));

    int lastNumSeeds = usingSeeds->size()+10;
    double lastTime, startTime = lastTime = util::estWallTime();
    ExecutionState *lastState = 0;
    while (!seedMap.empty()) {
      if (haltExecution) goto dump;

      std::map<ExecutionState*, std::vector<SeedInfo> >::iterator it = 
        seedMap.upper_bound(lastState);
      if (it == seedMap.end())
        it = seedMap.begin();
      lastState = it->first;
      unsigned numSeeds = it->second.size();
      ExecutionState &state = *lastState;
      KInstruction *ki = state.pc;
      stepInstruction(state);

      executeInstruction(state, ki);
      processTimers(&state, MaxInstructionTime * numSeeds);
      updateStates(&state);

      if ((stats::instructions % 1000) == 0) {
        int numSeeds = 0, numStates = 0;
        for (std::map<ExecutionState*, std::vector<SeedInfo> >::iterator
               it = seedMap.begin(), ie = seedMap.end();
             it != ie; ++it) {
          numSeeds += it->second.size();
          numStates++;
        }
        double time = util::estWallTime();
        if (SeedTime>0. && time > startTime + SeedTime) {
          klee_warning("seed time expired, %d seeds remain over %d states",
                       numSeeds, numStates);
          break;
        } else if (numSeeds<=lastNumSeeds-10 ||
                   time >= lastTime+10) {
          lastTime = time;
          lastNumSeeds = numSeeds;          
          klee_message("%d seeds remaining over: %d states", 
                       numSeeds, numStates);
        }
      }
    }

    klee_message("seeding done (%d states remain)", (int) states.size());

    // XXX total hack, just because I like non uniform better but want
    // seed results to be equally weighted.
    for (std::set<ExecutionState*>::iterator
           it = states.begin(), ie = states.end();
         it != ie; ++it) {
      (*it)->weight = 1.;
    }

    if (OnlySeed)
      goto dump;
  }

  searcher = constructUserSearcher(*this);
  
  searcher->update(0, states, std::set<ExecutionState*>(),std::set<ExecutionState*>(),std::set<ExecutionState*>());

  while (!states.empty() && !haltExecution) {
    ExecutionState *state = &searcher->selectState(!onlyNonCompact);
    assert((!onlyNonCompact || !state->isCompactForm)
      && "compact state chosen");

    /*if (CallInst* ci = dyn_cast<CallInst>(state->pc->inst)) {
      if (Function* f = ci->getCalledFunction()) {
        if (funcs.find(f) == funcs.end()) {
          funcs.insert(f);
          std::cout << "CALL " << f->getNameStr() << std::endl;
        }
      }
    }*/

    /*if (state->rec) {
    if (CallInst* ci = dyn_cast<CallInst>(state->pc->inst)) {
      if (Function* f = ci->getCalledFunction()) {
        if (f->getNameStr() == "version_etc_va") {
          StateRecord* rec = state->rec;
          if (state->prunepoint) {
            std::list<StateRecord*> term;
            equivStateElim->terminate(rec, term);
          while (rec != state->prunepoint) {
            rec->print();
            rec = rec->parent;
          }
          }
          exit(1);
        }
      }
    }
    }*/

    if (state->isCompactForm) {
      ExecutionState* newState = state->reconstitute(*initialStateCopy);
      replaceState(state, newState);
      updateStates(state);
      state = newState;
    }

    state->lastChosen = stats::instructions;

    KInstruction *ki = state->pc;
    assert(ki);

    if (state->rec) {
        state->rec->curinst = ki->inst;
    }

    stepInstruction(*state);

    executeInstruction(*state, ki);

    processTimers(state, MaxInstructionTime);

    if (MaxMemory) {
      if ((stats::instructions & 0xFFFF) == 0) {
        // We need to avoid calling GetMallocUsage() often because it
        // is O(elts on freelist). This is really bad since we start
        // to pummel the freelist once we hit the memory cap.
        unsigned mbs = sys::Process::GetTotalMemoryUsage() >> 20;
        
        if (mbs > MaxMemory) {
          atMemoryLimit = true;
          onlyNonCompact = true;

          if (mbs > MaxMemory + 100) {
            unsigned numStates = states.size();

            if (!ReplayInhibitedForks ||
                /* resort to killing states if the recent compacting
                   didn't help to reduce the memory usage */
                stats::instructions
                  - lastMemoryLimitOperationInstructions <= 0x20000)
            {
              // just guess at how many to kill
              unsigned toKill =
                std::max(1U, numStates - numStates * MaxMemory / mbs);
              klee_warning("killing %u states (over memory cap)", toKill);

              std::vector<ExecutionState*> arr(states.begin(), states.end());

              // use priority ordering for selecting which states to kill
              std::partial_sort(arr.begin(), arr.begin() + toKill, arr.end(),
                KillOrCompactOrdering());
              for (unsigned i = 0; i < toKill; ++i) {
                terminateStateEarly(*arr[i], "memory limit");
		if (state == arr[i]) state = NULL;
              }
            } else {
              // compact instead of killing
              std::vector<ExecutionState*> arr(nonCompactStateCount);
              unsigned i = 0;
              for (std::set<ExecutionState*>::iterator si = states.begin();
                   si != states.end(); ++si) {
                if(!(*si)->isCompactForm) {
                  arr[i] = *si;
                  i++;
                }
              }

              unsigned s = nonCompactStateCount +
                ((numStates - nonCompactStateCount) / 16); // a rough measure
              unsigned toCompact =
                std::max(1U, s - s * MaxMemory / mbs);
              toCompact = std::min(toCompact, (unsigned) nonCompactStateCount);
              klee_warning("compacting %u states (over memory cap)", toCompact);

              std::partial_sort(arr.begin(), arr.begin() + toCompact, arr.end(),
                KillOrCompactOrdering());
              for (unsigned i = 0; i < toCompact; ++i) {
                ExecutionState* original = arr[i];
                ExecutionState* compacted = original->compact();
                compacted->coveredNew = false;
                compacted->coveredLines.clear();
                compacted->ptreeNode = original->ptreeNode;
                replaceState(original, compacted);
		if (state == original) state = compacted;
              }
            }
            lastMemoryLimitOperationInstructions = stats::instructions;
          } // if (mbs > MaxMemory + 100)
        } else if (mbs < 0.9*MaxMemory) { // if (MaxMemory)
          atMemoryLimit = false;
        }
      }
    }

    updateStates(state);
  }

  if (equivStateElim) {
      equivStateElim->complete();
  }

  delete searcher;
  searcher = 0;
  
 dump:
  if (!states.empty()) {
    std::cerr << "KLEE: halting execution, dumping remaining states\n";
    for (std::set<ExecutionState*>::iterator
           it = states.begin(), ie = states.end();
         it != ie; ++it) {
      ExecutionState &state = **it;
      stepInstruction(state); // keep stats rolling
      if (DumpStatesOnHalt)
        terminateStateEarly(state, "execution halting");
      else
        terminateState(state);
    }
    updateStates(0);
  }

  delete initialStateCopy;
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

  std::set<ExecutionState*>::iterator it = addedStates.find(&state);
  if (it==addedStates.end()) {
    state.pc = state.prevPC;

    removedStates.insert(&state);
  } else {
    // never reached searcher, just delete immediately
    std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it3 = 
      seedMap.find(&state);
    if (it3 != seedMap.end())
      seedMap.erase(it3);
    addedStates.erase(it);
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
  
  if (EmitAllErrors ||
      emittedErrors.insert(std::make_pair(state.prevPC->inst, message)).second) {
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
    unsigned idx = 0;
    const KInstruction *target = state.prevPC;
    for (ExecutionState::stack_ty::reverse_iterator
           it = state.stack.rbegin(), ie = state.stack.rend();
         it != ie; ++it) {
      StackFrame &sf = *it;
      Function *f = sf.kf->function;
      const InstructionInfo &ii = *target->info;
      msg << "\t#" << idx++ 
          << " " << std::setw(8) << std::setfill('0') << ii.assemblyLine
          << " in " << f->getNameStr() << " (";
      // Yawn, we could go up and print varargs if we wanted to.
      unsigned index = 0;
      for (Function::arg_iterator ai = f->arg_begin(), ae = f->arg_end();
           ai != ae; ++ai) {
        if (ai!=f->arg_begin()) msg << ", ";

        msg << ai->getNameStr();
        // XXX should go through function
        //ref<Expr> value = sf.locals[sf.kf->getArgRegister(index++)].value;
        ref<Expr> value = state.getLocalCell(state.stack.size() - idx, sf.kf->getArgRegister(index++)).value;
        if (isa<ConstantExpr>(value))
          msg << "=" << value;
      }
      msg << ")";
      if (ii.file != "")
        msg << " at " << ii.file << ":" << ii.line;
      msg << "\n";
      target = sf.caller;
    }

    std::string info_str = info.str();
    if (info_str != "")
      msg << "Info: \n" << info_str;
    interpreterHandler->processTestCase(state, msg.str().c_str(), suffix);
  }
    
  terminateState(state);
}

// XXX shoot me
static const char *okExternalsList[] = { "printf", 
                                         "fprintf", 
                                         "puts",
                                         "getpid" };
static std::set<std::string> okExternals(okExternalsList,
                                         okExternalsList + 
                                         (sizeof(okExternalsList)/sizeof(okExternalsList[0])));

void Executor::callExternalFunction(ExecutionState &state,
                                    KInstruction *target,
                                    Function *function,
                                    std::vector< ref<Expr> > &arguments) {  
  // check if specialFunctionHandler wants it
  if (specialFunctionHandler->handle(state, function, target, arguments))
    return;
  
  if (NoExternals && !okExternals.count(function->getName())) {
    std::cerr << "KLEE:ERROR: Calling not-OK external function : " 
               << function->getNameStr() << "\n";
    terminateStateOnError(state, "externals disallowed", "user.err");
    return;
  }

  // normal external function handling path
  // allocate 128 bits for each argument (+return value) to support fp80's;
  // we could iterate through all the arguments first and determine the exact
  // size we need, but this is faster, and the memory usage isn't significant.
  uint64_t *args = (uint64_t*) alloca(2*sizeof(*args) * (arguments.size() + 1));
  memset(args, 0, 2 * sizeof(*args) * (arguments.size() + 1));
  unsigned wordIndex = 2;
  for (std::vector<ref<Expr> >::iterator ai = arguments.begin(), 
       ae = arguments.end(); ai!=ae; ++ai) {
    if (AllowExternalSymCalls) { // don't bother checking uniqueness
      ref<ConstantExpr> ce;
      bool success = solver->getValue(state, *ai, ce);
      assert(success && "FIXME: Unhandled solver failure");
      (void) success;
      ce->toMemory(&args[wordIndex]);
      wordIndex += (ce->getWidth()+63)/64;
    } else {
      ref<Expr> arg = toUnique(state, *ai);
      if (ConstantExpr *ce = dyn_cast<ConstantExpr>(arg)) {
        // XXX kick toMemory functions from here
        ce->toMemory(&args[wordIndex]);
        wordIndex += (ce->getWidth()+63)/64;
      } else {
        terminateStateOnExecError(state, 
                                  "external call with symbolic argument: " + 
                                  function->getName());
        return;
      }
    }
  }

  state.addressSpace.copyOutConcretes();

  if (!SuppressExternalWarnings) {
    std::ostringstream os;
    os << "calling external: " << function->getNameStr() << "(";
    for (unsigned i=0; i<arguments.size(); i++) {
      os << arguments[i];
      if (i != arguments.size()-1)
	os << ", ";
    }
    os << ")";
    
    if (AllExternalWarnings)
      klee_warning("%s", os.str().c_str());
    else
      klee_warning_once(function, "%s", os.str().c_str());
  }
  
  bool success = externalDispatcher->executeCall(function, target->inst, args);
  if (!success) {
    terminateStateOnError(state, "failed external call: " + function->getName(),
                          "external.err");
    return;
  }

  if (!state.addressSpace.copyInConcretes(state.rec)) {
    terminateStateOnError(state, "external modified read-only object",
                          "external.err");
    return;
  }

  const Type *resultType = target->inst->getType();
  if (resultType != Type::getVoidTy(getGlobalContext())) {
    ref<Expr> e = ConstantExpr::fromMemory((void*) args, 
                                           Expr::getWidthForLLVMType(resultType));
    bindLocal(target, state, e);
  }
}

/***/

ref<Expr> Executor::replaceReadWithSymbolic(ExecutionState &state, 
                                            ref<Expr> e) {
  unsigned n = interpreterOpts.MakeConcreteSymbolic;
  if (!n || replayOut || replayPaths)
    return e;

  // right now, we don't replace symbolics (is there any reason too?)
  if (!isa<ConstantExpr>(e))
    return e;

  if (n != 1 && random() %  n)
    return e;

  // create a new fresh location, assert it is equal to concrete value in e
  // and return it.
  
  static unsigned id;
  const Array *array =
    new Array("rrws_arr" + llvm::utostr(++id), 
              MallocKey(Expr::getMinBytesForWidth(e->getWidth())));
  ref<Expr> res = Expr::createTempRead(array, e->getWidth());
  ref<Expr> eq = NotOptimizedExpr::create(EqExpr::create(e, res));
  std::cerr << "Making symbolic: " << eq << "\n";
  state.addConstraint(eq);
  return res;
}

ObjectState *Executor::bindObjectInState(ExecutionState &state, 
                                         const MemoryObject *mo,
                                         bool isLocal,
                                         const Array *array) {
  ObjectState *os = array ? new ObjectState(mo, array) : new ObjectState(mo, state.rec);
  state.bindObject(mo, os);

  // Its possible that multiple bindings of the same mo in the state
  // will put multiple copies on this list, but it doesn't really
  // matter because all we use this list for is to unbind the object
  // on function return.
  if (isLocal)
    state.stack.back().allocas.push_back(mo);

  return os;
}

void Executor::executeAllocConst(
  ExecutionState &state,
  ConstantExpr* CE,
  bool isLocal,
  KInstruction *target,
  bool zeroMemory,
  const ObjectState *reallocFrom) 
{

  MemoryObject *mo = memory->allocate(CE->getZExtValue(), isLocal, false, 
                                      state.prevPC->inst, &state);
  if (!mo) {
    bindLocal(target, state, 
              ConstantExpr::alloc(0, Context::get().getPointerWidth()));
  } else {
    ObjectState *os = bindObjectInState(state, mo, isLocal);
    if (zeroMemory) {
      os->initializeToZero();
    } else {
      os->initializeToRandom();
    }
    bindLocal(target, state, mo->getBaseExpr());
    
    if (reallocFrom) {
      unsigned count = std::min(reallocFrom->size, os->size);

      state.copy(os, reallocFrom, count);
      /*(for (unsigned i=0; i<count; i++) {
        //os->write(i, reallocFrom->read8(i));          
        //state.write(os, i, state.read8(reallocFrom, i));
      }*/
      state.addressSpace.unbindObject(reallocFrom->getObject());
    }
  }
}


void Executor::executeAllocSymbolic(
  ExecutionState &state,
  ref<Expr> size,
  bool isLocal,
  KInstruction *target,
  bool zeroMemory,
  const ObjectState *reallocFrom)
{
  // XXX For now we just pick a size. Ideally we would support
  // symbolic sizes fully but even if we don't it would be better to
  // "smartly" pick a value, for example we could fork and pick the
  // min and max values and perhaps some intermediate (reasonable
  // value).
  // 
  // It would also be nice to recognize the case when size has
  // exactly two values and just fork (but we need to get rid of
  // return argument first). This shows up in pcre when llvm
  // collapses the size expression with a select.

  ref<ConstantExpr> example;
  bool success = solver->getValue(state, size, example);
  assert(success && "FIXME: Unhandled solver failure");
  (void) success;
  
  // Try and start with a small example.
  Expr::Width W = example->getWidth();
  while (example->Ugt(ConstantExpr::alloc(128, W))->isTrue()) {
    ref<ConstantExpr> tmp = example->LShr(ConstantExpr::alloc(1, W));
    bool res;
    bool success = solver->mayBeTrue(state, EqExpr::create(tmp, size), res);
    assert(success && "FIXME: Unhandled solver failure");      
    (void) success;
    if (!res)
      break;
    example = tmp;
  }

  StatePair fixedSize = fork(state, EqExpr::create(example, size), true);
  
  if (fixedSize.second) { 
    // Check for exactly two values
    ref<ConstantExpr> tmp;
    bool success = solver->getValue(*fixedSize.second, size, tmp);
    assert(success && "FIXME: Unhandled solver failure");      
    (void) success;
    bool res;
    success = solver->mustBeTrue(*fixedSize.second, 
                                 EqExpr::create(tmp, size),
                                 res);
    assert(success && "FIXME: Unhandled solver failure");      
    (void) success;
    if (res) {
      executeAlloc(*fixedSize.second, tmp, isLocal,
                   target, zeroMemory, reallocFrom);
    } else {
      // See if a *really* big value is possible. If so assume
      // malloc will fail for it, so lets fork and return 0.
      StatePair hugeSize = 
        fork(*fixedSize.second, 
             UltExpr::create(ConstantExpr::alloc(1<<31, W), size), 
             true);
      if (hugeSize.first) {
        klee_message("NOTE: found huge malloc, returing 0");
        bindLocal(target, *hugeSize.first, 
                  ConstantExpr::alloc(0, Context::get().getPointerWidth()));
      }
      
      if (hugeSize.second) {
        std::ostringstream info;
        ExprPPrinter::printOne(info, "  size expr", size);
        info << "  concretization : " << example << "\n";
        info << "  unbound example: " << tmp << "\n";
        terminateStateOnError(*hugeSize.second, 
                              "concretized symbolic size", 
                              "model.err", 
                              info.str());
      }
    }
  }

  if (fixedSize.first) // can be zero when fork fails
    executeAlloc(*fixedSize.first, example, isLocal, 
                 target, zeroMemory, reallocFrom);
}

void Executor::executeAlloc(
  ExecutionState &state,
  ref<Expr> size,
  bool isLocal,
  KInstruction *target,
  bool zeroMemory,
  const ObjectState *reallocFrom) 
{
  size = toUnique(state, size);
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(size)) {
    executeAllocConst(state, CE, isLocal, target, zeroMemory, reallocFrom);
  } else {
    executeAllocSymbolic(state, size, isLocal, target, zeroMemory, reallocFrom);
  }
}

void Executor::executeFree(ExecutionState &state,
                           ref<Expr> address,
                           KInstruction *target) {
  StatePair zeroPointer = fork(state, Expr::createIsZero(address), true);
  if (zeroPointer.first) {
    if (target)
      bindLocal(target, *zeroPointer.first, Expr::createPointer(0));
  }
  if (!zeroPointer.second) return;
  
  // address != 0
  ExactResolutionList rl;
  resolveExact(*zeroPointer.second, address, rl, "free");
  
  for (Executor::ExactResolutionList::iterator it = rl.begin(), 
         ie = rl.end(); it != ie; ++it) {
    const MemoryObject *mo = it->first.first;
    if (mo->isLocal()) {
      terminateStateOnError(*it->second, 
                            "free of alloca", 
                            "free.err",
                            getAddressInfo(*it->second, address));
    } else if (mo->isGlobal()) {
      terminateStateOnError(*it->second, 
                            "free of global", 
                            "free.err",
                            getAddressInfo(*it->second, address));
    } else {
      it->second->addressSpace.unbindObject(mo);
      if (target)
        bindLocal(target, *it->second, Expr::createPointer(0));
    }
  }
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

bool Executor::memOpFast(
  ExecutionState& state,
  bool isWrite,
  ref<Expr> address,
  ref<Expr> value,
  KInstruction* target)
{
  Expr::Width type = (isWrite ? value->getWidth() : 
                     Expr::getWidthForLLVMType(target->inst->getType()));
  unsigned bytes = Expr::getMinBytesForWidth(type);
  ObjectPair op;
  ref<Expr> offset;
  bool inBounds, success;

  solver->setTimeout(stpTimeout);
  if (!state.addressSpace.resolveOne(state, solver, address, op, success)) {
    address = toConstant(state, address, "resolveOne failure");
    success = state.addressSpace.resolveOne(cast<ConstantExpr>(address), op);
  }
  solver->setTimeout(0);

  const MemoryObject *mo = op.first;
  const ObjectState *os = op.second;

  if (!success) return false;
  // fast path: single in-bounds resolution

  if (MaxSymArraySize && mo->size>=MaxSymArraySize) {
    address = toConstant(state, address, "max-sym-array-size");
  }
  
  offset = mo->getOffsetExpr(address);

  solver->setTimeout(stpTimeout);
  success = solver->mustBeTrue(
    state, mo->getBoundsCheckOffset(offset, bytes), inBounds);
  solver->setTimeout(0);
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
      //wos->write(offset, value);
      state.write(wos, offset, value);
    }          
  } else {
    //ref<Expr> result = os->read(offset, type);
    ref<Expr> result = state.read(os, offset, type);

  /* XXX AJR */
    std::ostringstream info;
    result->print(info);
    std::string res = info.str();    

    std::ostringstream inf;
    offset->print(inf);
    std::string res_addr = inf.str();    
    klee_message("Read: off=%s result=%s %d.", res_addr.c_str(), res.c_str(), bytes);
    if (interpreterOpts.MakeConcreteSymbolic)
      result = replaceReadWithSymbolic(state, result);
    
    bindLocal(target, state, result);
  }

  return true;
}

void Executor::memOpError(
  ExecutionState& state,
  bool isWrite,
  ref<Expr> address,
  ref<Expr> value,
  KInstruction* target)
{
  Expr::Width type = (isWrite ? value->getWidth() : 
                     Expr::getWidthForLLVMType(target->inst->getType()));
  unsigned bytes = Expr::getMinBytesForWidth(type);
  ResolutionList rl;  
  ExecutionState *unbound;
  bool incomplete;

  solver->setTimeout(stpTimeout);
  incomplete = state.addressSpace.resolve(state, solver, address, rl, 0, stpTimeout);
  solver->setTimeout(0);
  
  // XXX there is some query wasteage here. who cares?
  unbound = &state;
  for (ResolutionList::iterator it = rl.begin(), ie = rl.end(); it != ie; ++it) {
    const MemoryObject *mo;
    const ObjectState *os;

    mo = it->first;
    os = it->second;
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

    unbound = branches.second;
    if (!unbound) break;
  }
  
  if (!unbound) return;

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
  const Array *array = new Array("arr" + llvm::utostr(++id),
                                 mo->mallocKey, 0, 0, state.rec);
  array->initRef();
  bindObjectInState(state, mo, false, array);
  state.addSymbolic(mo, array, len);
  
  std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it = 
    seedMap.find(&state);
  if (it == seedMap.end()) return;
  
  // In seed mode we need to add this as a binding.
  for (std::vector<SeedInfo>::iterator siit = it->second.begin(), 
    siie = it->second.end(); siit != siie; ++siit)
  {
    if (!seedObject(state, *siit, mo, array))
      break;
  }
}

/***/

void Executor::runFunctionAsMain(Function *f,
				 int argc,
				 char **argv,
				 char **envp) {
  std::vector<ref<Expr> > arguments;

  // force deterministic initialization of memory objects
  srand(1);
  srandom(1);
  
  MemoryObject *argvMO = 0;

  // In order to make uclibc happy and be closer to what the system is
  // doing we lay out the environments at the end of the argv array
  // (both are terminated by a null). There is also a final terminating
  // null that uclibc seems to expect, possibly the ELF header?

  int envc;
  for (envc=0; envp[envc]; ++envc) ;

  unsigned NumPtrBytes = Context::get().getPointerWidth() / 8;
  KFunction *kf = kmodule->functionMap[f];
  assert(kf);

  ExecutionState *state = new ExecutionState(kmodule->functionMap[f]);
  if (UseEquivalentStateEliminator) {
    equivStateElim = new EquivalentStateEliminator(this, kmodule, states);
    std::set<ExecutionState*> tmp;
    equivStateElim->setup(state, tmp);
    assert(tmp.empty());
  }

  Function::arg_iterator ai = f->arg_begin(), ae = f->arg_end();
  if (ai!=ae) {
    arguments.push_back(ConstantExpr::alloc(argc, Expr::Int32));

    if (++ai!=ae) {
      argvMO = memory->allocate((argc+1+envc+1+1) * NumPtrBytes, false, true,
                                f->begin()->begin(), state);
      
      arguments.push_back(argvMO->getBaseExpr());

      if (++ai!=ae) {
        uint64_t envp_start = argvMO->address + (argc+1)*NumPtrBytes;
        arguments.push_back(Expr::createPointer(envp_start));

        if (++ai!=ae)
          klee_error("invalid main function (expect 0-3 arguments)");
      }
    }
  }
  
  if (symPathWriter)
    state->symPathOS = symPathWriter->open();

  if (statsTracker)
    statsTracker->framePushed(*state, 0);

  assert(arguments.size() == f->arg_size() && "wrong number of arguments");
  for (unsigned i = 0, e = f->arg_size(); i != e; ++i)
    bindArgument(kf, i, *state, arguments[i]);

  if (argvMO) {
    ObjectState *argvOS = bindObjectInState(*state, argvMO, false);

    for (int i=0; i<argc+1+envc+1+1; i++) {
      MemoryObject *arg;
      
      if (i==argc || i>=argc+1+envc) {
        arg = 0;
      } else {
        char *s = i<argc ? argv[i] : envp[i-(argc+1)];
        int j, len = strlen(s);
        
        arg = memory->allocate(len+1, false, true, state->pc->inst, state);
        ObjectState *os = bindObjectInState(*state, arg, false);
        for (j=0; j<len+1; j++) {
          //os->write8(j, s[j]);
          state->write8(os, j, s[j]);
        }
      }

      if (arg) {
        //argvOS->write(i * NumPtrBytes, arg->getBaseExpr());
        state->write(argvOS, i * NumPtrBytes, arg->getBaseExpr());
      } else {
        //argvOS->write(i * NumPtrBytes, Expr::createPointer(0));
        state->write(argvOS, i * NumPtrBytes, Expr::createPointer(0));
      }
    }
  }
  
  initializeGlobals(*state);

  processTree = new PTree(state);
  state->ptreeNode = processTree->root;
  run(*state);
  delete processTree;
  processTree = 0;

  // hack to clear memory objects
  delete memory;
  memory = new MemoryManager();
  
  globalObjects.clear();
  globalAddresses.clear();

  if (statsTracker)
    statsTracker->done();

  if (theMMap) {
    munmap(theMMap, theMMapSize);
    theMMap = 0;
  }
}

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

bool Executor::getSymbolicSolution(const ExecutionState &state,
                                   std::vector< 
                                   std::pair<std::string,
                                   std::vector<unsigned char> > >
                                   &res) {
  solver->setTimeout(stpTimeout);

  ExecutionState tmp(state);
  if (!NoPreferCex) getSymbolicSolutionCex(state, tmp);

  std::vector< std::vector<unsigned char> > values;
  std::vector<const Array*> objects;
  for (unsigned i = 0; i != state.symbolics.size(); ++i)
    objects.push_back(state.symbolics[i].getArray());
  bool success = solver->getInitialValues(tmp, objects, values);
  solver->setTimeout(0);
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

void Executor::getCoveredLines(const ExecutionState &state,
                               std::map<const std::string*, std::set<unsigned> > &res) {
  res = state.coveredLines;
}

void Executor::doImpliedValueConcretization(ExecutionState &state,
                                            ref<Expr> e,
                                            ref<ConstantExpr> value) {
  abort(); // FIXME: Broken until we sort out how to do the write back.

  if (DebugCheckForImpliedValues)
    ImpliedValue::checkForImpliedValues(solver->solver, e, value);

  ImpliedValueList results;
  ImpliedValue::getImpliedValues(e, value, results);
  for (ImpliedValueList::iterator it = results.begin(), ie = results.end();
       it != ie; ++it) {
    ReadExpr *re = it->first.get();
    
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(re->index)) {
      // FIXME: This is the sole remaining usage of the Array object
      // variable. Kill me.
      const MemoryObject *mo = 0; //re->updates.root->object;
      const ObjectState *os = state.addressSpace.findObject(mo);

      if (!os) {
        // object has been free'd, no need to concretize (although as
        // in other cases we would like to concretize the outstanding
        // reads, but we have no facility for that yet)
      } else {
        assert(!os->readOnly && 
               "not possible? read only object with static read?");
        ObjectState *wos = state.addressSpace.getWriteable(mo, os);
        //wos->write(CE, it->second);
        state.write(wos, CE, it->second);
      }
    }
  }
}

///

Interpreter *Interpreter::create(const InterpreterOptions &opts,
                                 InterpreterHandler *ih) {
  return new Executor(opts, ih);
}

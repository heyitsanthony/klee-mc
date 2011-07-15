//===-- SpecialFunctionHandler.cpp ----------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Memory.h"
#include "SpecialFunctionHandler.h"
#include "TimingSolver.h"
#include "OpenfdRegistry.h"

#include "klee/Common.h"
#include "klee/ExecutionState.h"

#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "static/Sugar.h"

#include "Executor.h"
#include "MemoryManager.h"

#include "llvm/Module.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/CommandLine.h"

#include <errno.h>

using namespace llvm;
using namespace klee;

/// \todo Almost all of the demands in this file should be replaced
/// with terminateState calls.

// FIXME: We are more or less committed to requiring an intrinsic
// library these days. We can move some of this stuff there,
// especially things like realloc which have complicated semantics
// w.r.t. forking. Among other things this makes delayed query
// dispatch easier to implement.
SpecialFunctionHandler::HandlerInfo handlerInfo[] =
{
#define add(name, h, ret) {	\
	name, 			\
	&Handler##h::create,	\
	false, ret, false }
#define addDNR(name, h) {	\
	name, 			\
	&Handler##h::create,	\
	true, false, false }
  addDNR("__assert_rtn", AssertFail),
  addDNR("__assert_fail", AssertFail),
  addDNR("_assert",Assert),
  addDNR("abort", Abort),
  addDNR("_exit", Exit),
  { "exit", &HandlerExit::create, true, false, true },
  addDNR("klee_abort", Abort),
  addDNR("klee_silent_exit", SilentExit),
  addDNR("klee_report_error", ReportError),

  add("alarm", Alarm, true),
  add("calloc", Calloc, true),
  add("free", Free, false),
  add("klee_assume", Assume, false),
  add("klee_check_memory_access", CheckMemoryAccess, false),
  add("klee_get_value", GetValue, true),
  add("klee_define_fixed_object", DefineFixedObject, false),
  add("klee_get_obj_size", GetObjSize, true),
  add("klee_get_errno", GetErrno, true),
  add("klee_is_symbolic", IsSymbolic, true),
  add("klee_make_symbolic", MakeSymbolic, false),
  add("klee_mark_global", MarkGlobal, false),
  add("klee_mark_openfd", MarkOpenfd, false),
  add("klee_merge", Merge, false),
  add("klee_prefer_cex", PreferCex, false),
  add("klee_print_expr", PrintExpr, false),
  add("klee_print_range", PrintRange, false),
  add("klee_set_forking", SetForking, false),
  add("klee_stack_trace", StackTrace, false),
  add("klee_warning", Warning, false),
  add("klee_warning_once", WarningOnce, false),
  add("klee_alias_function", AliasFunction, false),
  add("klee_get_prune_id", GetPruneID, true),
  add("klee_prune", Prune, false),
  add("malloc", Malloc, true),
  add("realloc", Realloc, true),

  // operator delete[](void*)
  add("_ZdaPv", DeleteArray, false),
  // operator delete(void*)
  add("_ZdlPv", Delete, false),

  // operator new[](unsigned int)
  add("_Znaj", NewArray, true),
  // operator new(unsigned int)
  add("_Znwj", New, true),

  // FIXME-64: This is wrong for 64-bit long...

  // operator new[](unsigned long)
  add("_Znam", NewArray, true),
  // operator new(unsigned long)
  add("_Znwm", New, true),

#undef addDNR
#undef add
};

SpecialFunctionHandler::SpecialFunctionHandler(Executor* _executor)
  : executor(_executor) {}


void SpecialFunctionHandler::prepare()
{
  unsigned N = sizeof(handlerInfo)/sizeof(handlerInfo[0]);
  prepare((HandlerInfo*)(&handlerInfo), N);
}

void SpecialFunctionHandler::bind(void)
{
  unsigned N = sizeof(handlerInfo)/sizeof(handlerInfo[0]);
  bind((HandlerInfo*)&handlerInfo, N);
}

void SpecialFunctionHandler::prepare(HandlerInfo* hinfo, unsigned int N)
{
  for (unsigned i=0; i<N; ++i) {
    HandlerInfo &hi = hinfo[i];
    Function *f = executor->getKModule()->module->getFunction(hi.name);

    // No need to create if the function doesn't exist, since it cannot
    // be called in that case.
    if (!f) continue;
    if (!(!hi.doNotOverride || f->isDeclaration())) continue;

    // Make sure NoReturn attribute is set, for optimization and
    // coverage counting.
    if (hi.doesNotReturn)
      f->addFnAttr(Attribute::NoReturn);

    // Change to a declaration since we handle internally (simplifies
    // module and allows deleting dead code).
    if (!f->isDeclaration())
      f->deleteBody();
  }
}


SpecialFunctionHandler::~SpecialFunctionHandler(void)
{
	foreach (it, handlers.begin(), handlers.end()) {
		Handler	*h = (it->second).first;
		delete h;
	}
}

void SpecialFunctionHandler::bind(HandlerInfo* hinfo, unsigned int N)
{
  for (unsigned i=0; i<N; ++i) {
    HandlerInfo &hi = hinfo[i];
    Function *f = executor->getKModule()->module->getFunction(hi.name);

    if (f && (!hi.doNotOverride || f->isDeclaration())) {
    	Handler	*old_h = handlers[f].first;
	if (old_h) delete old_h;
        handlers[f] = std::make_pair(
      	  hi.handler_init(this),
          hi.hasReturnValue);
    }
  }
}


bool SpecialFunctionHandler::handle(
  ExecutionState &state,
  Function *f,
  KInstruction *target,
  std::vector< ref<Expr> > &arguments)
{
  handlers_ty::iterator it = handlers.find(f);
  if (it == handlers.end()) return false;

  Handler* h = it->second.first;
  bool hasReturnValue = it->second.second;
   // FIXME: Check this... add test?
  if (!hasReturnValue && !target->inst->use_empty()) {
    executor->terminateStateOnExecError(
    	state,
        "expected return value from void special function");
  } else {
    h->handle(state, target, arguments);
  }
  return true;
}

/****/

// reads a concrete string from memory
std::string
SpecialFunctionHandler::readStringAtAddress(
  ExecutionState &state,
  ref<Expr> addressExpr)
{
  ObjectPair op;
  ref<ConstantExpr> address;

  addressExpr = executor->toUnique(state, addressExpr);
  address = cast<ConstantExpr>(addressExpr);

  assert (address.get() && "Expected constant address");
  if (!state.addressSpace.resolveOne(address, op)) {
    assert(0 && "XXX out of bounds / multiple resolution unhandled");
  }

  const MemoryObject	*mo;
  const ObjectState	*os;
  char			*buf;
  uint64_t		offset;

  mo = op.first;
  os = op.second;

  offset = address->getZExtValue() - op.first->getBaseExpr()->getZExtValue();
  assert (offset < mo->size);

  buf = new char[(mo->size-offset)+1];

  unsigned i;
  for (i = offset; i < mo->size - 1; i++) {
    ref<Expr> cur;

    cur = state.read8(os, i);
    cur = executor->toUnique(state, cur);
    assert(isa<ConstantExpr>(cur) &&
           "hit symbolic char while reading concrete string");
    buf[i-offset] = cast<ConstantExpr>(cur)->getZExtValue(8);
    if (!buf[i-offset]) break;
  }
  buf[i-offset] = 0;

  std::string result(buf);
  delete[] buf;
  return result;
}

/****/

SFH_DEF_HANDLER(Abort)
{
  SFH_CHK_ARGS(0, "abort");

  //XXX:DRE:TAINT
  if(state.underConstrained) {
    std::cerr << "TAINT: skipping abort fail\n";
    sfh->executor->terminateState(state);
  } else {
    sfh->executor->terminateStateOnError(state, "abort failure", "abort.err");
  }
}

SFH_DEF_HANDLER(Exit)
{
  SFH_CHK_ARGS(1, "exit");
  sfh->executor->terminateStateOnExit(state);
}

SFH_DEF_HANDLER(SilentExit)
{
  assert(arguments.size()==1 && "invalid number of arguments to exit");
  sfh->executor->terminateState(state);
}

SFH_DEF_HANDLER(AliasFunction)
{
  SFH_CHK_ARGS(2, "iklee_alias_function");
  std::string old_fn = sfh->readStringAtAddress(state, arguments[0]);
  std::string new_fn = sfh->readStringAtAddress(state, arguments[1]);
  //std::cerr << "Replacing " << old_fn << "() with " << new_fn << "()\n";
  if (old_fn == new_fn)
    state.removeFnAlias(old_fn);
  else state.addFnAlias(old_fn, new_fn);
}

cl::opt<bool> EnablePruning("enable-pruning", cl::init(true));

static std::map<int, int> pruneMap;

SFH_DEF_HANDLER(GetPruneID)
{
  if (!EnablePruning) {
    state.bindLocal(target, ConstantExpr::create(0, Expr::Int32));
    return;
  }

  static unsigned globalPruneID = 1;
  SFH_CHK_ARGS(1, "klee_get_prune_id");
  int count = cast<ConstantExpr>(arguments[0])->getZExtValue();

  state.bindLocal(target, ConstantExpr::create(globalPruneID, Expr::Int32));
  pruneMap[globalPruneID] = count;

  globalPruneID++;
}

SFH_DEF_HANDLER(Prune)
{
  if (!EnablePruning)
    return;

  assert((arguments.size()==1 || arguments.size()==2) &&
	 "Usage: klee_prune(pruneID [,uniqueID])\n");
  int prune_id = cast<ConstantExpr>(arguments[0])->getZExtValue();

  assert(pruneMap.find(prune_id) != pruneMap.end() &&
	 "Invalid prune ID passed to klee_prune()");

  if (pruneMap[prune_id] == 0)
    sfh->executor->terminateState(state);
  else
    pruneMap[prune_id]--;
}

SFH_DEF_HANDLER(Assert)
{
  SFH_CHK_ARGS(3, "_assert");

  //XXX:DRE:TAINT
  if(state.underConstrained) {
    std::cerr << "TAINT: skipping assertion:"
               << sfh->readStringAtAddress(state, arguments[0]) << "\n";
    sfh->executor->terminateState(state);
  } else
    sfh->executor->terminateStateOnError(
      state,
      "ASSERTION FAIL: " + sfh->readStringAtAddress(state, arguments[0]),
      "assert.err");
}

SFH_DEF_HANDLER(AssertFail)
{
  assert(arguments.size()==4 && "invalid number of arguments to __assert_fail");

  //XXX:DRE:TAINT
  if(state.underConstrained) {
    std::cerr << "TAINT: skipping assertion:"
               << sfh->readStringAtAddress(state, arguments[0]) << "\n";
    sfh->executor->terminateState(state);
  } else
    sfh->executor->terminateStateOnError(state,
                                   "ASSERTION FAIL: " + sfh->readStringAtAddress(state, arguments[0]),
                                   "assert.err");
}

SFH_DEF_HANDLER(ReportError)
{
  SFH_CHK_ARGS(4, "klee_report_error");

  // arg[0] = file
  // arg[1] = line
  // arg[2] = message
  // arg[3] = suffix
  std::string	message = sfh->readStringAtAddress(state, arguments[2]);
  std::string	suffix = sfh->readStringAtAddress(state, arguments[3]);

  //XXX:DRE:TAINT
  if(state.underConstrained) {
    std::cerr << "TAINT: skipping klee_report_error:"
               << message << ":" << suffix << "\n";
    sfh->executor->terminateState(state);
    return;
  }

  sfh->executor->terminateStateOnError(state, message, suffix.c_str());
}

SFH_DEF_HANDLER(Merge)
{
  // nop
}

SFH_DEF_HANDLER(New)
{
  // XXX should type check args
  SFH_CHK_ARGS(1, "new");
  sfh->executor->executeAlloc(state, arguments[0], false, target);
}

SFH_DEF_HANDLER(Delete)
{
  // FIXME: Should check proper pairing with allocation type (malloc/free,
  // new/delete, new[]/delete[]).

  // XXX should type check args
  SFH_CHK_ARGS(1, "delete");
  sfh->executor->executeFree(state, arguments[0]);
}

SFH_DEF_HANDLER(NewArray)
{
  // XXX should type check args
  SFH_CHK_ARGS(1, "new[]");
  sfh->executor->executeAlloc(state, arguments[0], false, target);
}

SFH_DEF_HANDLER(DeleteArray)
{
  // XXX should type check args
  SFH_CHK_ARGS(1, "delete[]");
  sfh->executor->executeFree(state, arguments[0]);
}

SFH_DEF_HANDLER(Malloc)
{
  // XXX should type check args
  SFH_CHK_ARGS(1, "malloc");
  sfh->executor->executeAlloc(state, arguments[0], false, target);
}

SFH_DEF_HANDLER(Assume)
{
  bool res;
  bool success;

  SFH_CHK_ARGS(1, "klee_assume");

  ref<Expr> e = arguments[0];

  if (e->getWidth() != Expr::Bool) {
    e = NeExpr::create(e, ConstantExpr::create(0, e->getWidth()));
  }

  success = sfh->executor->getSolver()->mustBeFalse(state, e, res);
  assert(success && "FIXME: Unhandled solver failure");
  if (res) {
    sfh->executor->terminateStateOnError(
      state,
      "invalid klee_assume call (provably false)",
      "user.err");
  } else {
    sfh->executor->addConstraint(state, e);
  }
}

SFH_DEF_HANDLER(IsSymbolic)
{
  SFH_CHK_ARGS(1, "klee_is_symbolic");

  state.bindLocal(target,
  	ConstantExpr::create(
		!isa<ConstantExpr>(arguments[0]),
		Expr::Int32));
}

SFH_DEF_HANDLER(PreferCex)
{
  SFH_CHK_ARGS(2, "klee_prefex_cex");

  ref<Expr> cond = arguments[1];
  if (cond->getWidth() != Expr::Bool)
    cond = NeExpr::create(cond, ConstantExpr::alloc(0, cond->getWidth()));

  Executor::ExactResolutionList rl;
  sfh->executor->resolveExact(state, arguments[0], rl, "prefex_cex");

  assert(rl.size() == 1 &&
         "prefer_cex target must resolve to precisely one object");

  rl[0].first.first->cexPreferences.push_back(cond);
}

SFH_DEF_HANDLER(PrintExpr)
{
  SFH_CHK_ARGS(2, "klee_print_expr");

  std::string msg_str = sfh->readStringAtAddress(state, arguments[0]);
  std::cerr << msg_str << ":" << arguments[1] << "\n";
}

SFH_DEF_HANDLER(SetForking)
{
  SFH_CHK_ARGS(1, "klee_set_forking");
  ref<Expr> value = sfh->executor->toUnique(state, arguments[0]);

  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(value)) {
    state.forkDisabled = CE->isZero();
  } else {
    sfh->executor->terminateStateOnError(state,
                                   "klee_set_forking requires a constant arg",
                                   "user.err");
  }
}

SFH_DEF_HANDLER(StackTrace)
{
  state.dumpStack(std::cout);
}

SFH_DEF_HANDLER(Warning)
{
  SFH_CHK_ARGS(1, "klee_warning");

  std::string msg_str = sfh->readStringAtAddress(state, arguments[0]);
  klee_warning("%s: %s", state.getCurrentKFunc()->function->getName().data(),
               msg_str.c_str());
}

SFH_DEF_HANDLER(WarningOnce)
{
  SFH_CHK_ARGS(1, "klee_warning_once");

  std::string msg_str = sfh->readStringAtAddress(state, arguments[0]);
  klee_warning_once(0, "%s: %s", state.getCurrentKFunc()->function->getName().data(),
                    msg_str.c_str());
}

SFH_DEF_HANDLER(PrintRange)
{
  SFH_CHK_ARGS(2, "klee_print_range");

  std::string msg_str = sfh->readStringAtAddress(state, arguments[0]);
  std::cerr << msg_str << ":" << arguments[1];
  if (!isa<ConstantExpr>(arguments[1])) {
    // FIXME: Pull into a unique value method?
    ref<ConstantExpr> value;
    bool success = sfh->executor->getSolver()->getValue(state, arguments[1], value);
    assert(success && "FIXME: Unhandled solver failure");
    bool res;
    success = sfh->executor->getSolver()->mustBeTrue(state,
                                          EqExpr::create(arguments[1], value),
                                          res);
    assert(success && "FIXME: Unhandled solver failure");
    if (res) {
      std::cerr << " == " << value;
    } else {
      std::cerr << " ~= " << value;
      std::pair< ref<Expr>, ref<Expr> > res =
        sfh->executor->getSolver()->getRange(state, arguments[1]);
      std::cerr << " (in [" << res.first << ", " << res.second <<"])";
    }
  }
  std::cerr << "\n";
}

SFH_DEF_HANDLER(GetObjSize)
{
  // XXX should type check args
  SFH_CHK_ARGS(1, "klee_get_obj_size");
  Executor::ExactResolutionList rl;
  sfh->executor->resolveExact(state, arguments[0], rl, "klee_get_obj_size");
  foreach (it, rl.begin(), rl.end()) {
    it->second->bindLocal(
    	target,
        ConstantExpr::create(it->first.first->size, Expr::Int32));
  }
}

SFH_DEF_HANDLER(GetErrno)
{
  // XXX should type check args
  SFH_CHK_ARGS(0, "klee_get_errno");
  state.bindLocal(target, ConstantExpr::create(errno, Expr::Int32));
}

SFH_DEF_HANDLER(Calloc)
{
  // XXX should type check args
  SFH_CHK_ARGS(2, "calloc");

  ref<Expr> size = MulExpr::create(arguments[0],
                                   arguments[1]);
  sfh->executor->executeAlloc(state, size, false, target, true);
}

SFH_DEF_HANDLER(Realloc)
{
  // XXX should type check args
  SFH_CHK_ARGS(2, "realloc");
  ref<Expr> address = arguments[0];
  ref<Expr> size = arguments[1];

  Executor::StatePair zeroSize = sfh->executor->fork(state,
                                               Expr::createIsZero(size),
                                               true);

  if (zeroSize.first) { // size == 0
    sfh->executor->executeFree(*zeroSize.first, address, target);
  }
  if (zeroSize.second) { // size != 0
    Executor::StatePair zeroPointer = sfh->executor->fork(*zeroSize.second,
                                                    Expr::createIsZero(address),
                                                    true);

    if (zeroPointer.first) { // address == 0
      sfh->executor->executeAlloc(*zeroPointer.first, size, false, target);
    }
    if (zeroPointer.second) { // address != 0
      Executor::ExactResolutionList rl;
      sfh->executor->resolveExact(*zeroPointer.second, address, rl, "realloc");

      for (Executor::ExactResolutionList::iterator it = rl.begin(),
             ie = rl.end(); it != ie; ++it) {
        sfh->executor->executeAlloc(*it->second, size, false, target, false,
                              it->first.second);
      }
    }
  }
}

SFH_DEF_HANDLER(Free)
{
  // XXX should type check args
  SFH_CHK_ARGS(1, "free");
  sfh->executor->executeFree(state, arguments[0]);
}

SFH_DEF_HANDLER(CheckMemoryAccess)
{
  SFH_CHK_ARGS(2, "klee_check_memory_access");

  ref<Expr> address = sfh->executor->toUnique(state, arguments[0]);
  ref<Expr> size = sfh->executor->toUnique(state, arguments[1]);
  if (!isa<ConstantExpr>(address) || !isa<ConstantExpr>(size)) {
    sfh->executor->terminateStateOnError(state,
                                   "check_memory_access requires constant args",
                                   "user.err");
  } else {
    ObjectPair op;

    if (!state.addressSpace.resolveOne(cast<ConstantExpr>(address), op)) {
      sfh->executor->terminateStateOnError(state,
                                     "check_memory_access: memory error",
                                     "ptr.err",
                                     sfh->executor->getAddressInfo(state, address));
    } else {
      ref<Expr> chk =
        op.first->getBoundsCheckPointer(address,
                                        cast<ConstantExpr>(size)->getZExtValue());
      if (!chk->isTrue()) {
        sfh->executor->terminateStateOnError(state,
                                       "check_memory_access: memory error",
                                       "ptr.err",
                                       sfh->executor->getAddressInfo(state, address));
      }
    }
  }
}

SFH_DEF_HANDLER(GetValue)
{
  SFH_CHK_ARGS(1, "klee_get_value");

  sfh->executor->executeGetValue(state, arguments[0], target);
}

SFH_DEF_HANDLER(DefineFixedObject)
{
  SFH_CHK_ARGS(2, "klee_define_fixed_object");
  assert(isa<ConstantExpr>(arguments[0]) &&
         "expect constant address argument to klee_define_fixed_object");
  assert(isa<ConstantExpr>(arguments[1]) &&
         "expect constant size argument to klee_define_fixed_object");

  uint64_t address = cast<ConstantExpr>(arguments[0])->getZExtValue();
  uint64_t size = cast<ConstantExpr>(arguments[1])->getZExtValue();
  MemoryObject *mo = sfh->executor->memory->allocateFixed(address, size,
                                                    state.prevPC->inst, &state);
  state.bindMemObj(mo);
  mo->isUserSpecified = true; // XXX hack;
}

#define MAKESYM_ARGIDX_ADDR   0
#define MAKESYM_ARGIDX_LEN    1
#define MAKESYM_ARGIDX_NAME   2
SFH_DEF_HANDLER(MakeSymbolic)
{
  std::string name;

  // FIXME: For backwards compatibility, we should eventually enforce the
  // correct arguments.
  if (arguments.size() == 2) {
    name = "unnamed";
  } else {
    // FIXME: Should be a user.err, not an assert.
    SFH_CHK_ARGS(3, "klee_make_symbolic");
    name = sfh->readStringAtAddress(state, arguments[MAKESYM_ARGIDX_NAME]);
  }

  Executor::ExactResolutionList rl;
  sfh->executor->resolveExact(state, arguments[MAKESYM_ARGIDX_ADDR], rl, "make_symbolic");

  foreach (it, rl.begin(), rl.end()) {
    MemoryObject *mo = (MemoryObject*) it->first.first;
    const ObjectState *old = it->first.second;
    ExecutionState *s = it->second;
    bool res, success;

    mo->setName(name);

    if (old->readOnly) {
      sfh->executor->terminateStateOnError(
        *s, "cannot make readonly object symbolic", "user.err");
      return;
    }

    // FIXME: Type coercion should be done consistently somewhere.
    // UleExpr instead of EqExpr to make a symbol partially symbolic.
    success = sfh->executor->getSolver()->mustBeTrue(
      *s,
      UleExpr::create(
        ZExtExpr::create(
          arguments[MAKESYM_ARGIDX_LEN],
          Context::get().getPointerWidth()),
        mo->getSizeExpr()),
      res);
    assert(success && "FIXME: Unhandled solver failure");

    if (res) {
      sfh->executor->executeMakeSymbolic(*s, mo);
    } else {
      sfh->executor->terminateStateOnError(
        *s, "size given to klee_make_symbolic[_name] too big", "user.err");
    }
  }
}

SFH_DEF_HANDLER(Alarm)
{
  SFH_CHK_ARGS(1, "alarm");

  klee_warning_once(0, "ignoring alarm()");
  state.bindLocal(target, ConstantExpr::create(0, Expr::Int32));
}

SFH_DEF_HANDLER(MarkGlobal)
{
  SFH_CHK_ARGS(1, "klee_mark_global");

  Executor::ExactResolutionList rl;
  sfh->executor->resolveExact(state, arguments[0], rl, "mark_global");

  for (Executor::ExactResolutionList::iterator it = rl.begin(),
         ie = rl.end(); it != ie; ++it) {
    MemoryObject *mo = (MemoryObject*) it->first.first;
    assert(!mo->isLocal());
    mo->setGlobal(true);
  }
}

SFH_DEF_HANDLER(MarkOpenfd)
{
  SFH_CHK_ARGS(1, "klee_mark_openfd");

  int fd = cast<ConstantExpr>(arguments[0])->getZExtValue();
  OpenfdRegistry::fdOpened(&state, fd);
}

#ifndef KLEE_STACKFRAME_H
#define KLEE_STACKFRAME_H

#include "klee/Constraints.h"
#include "klee/Expr.h"
#include "klee/util/ExprUtil.h"
#include "klee/Internal/Module/Cell.h"
// FIXME: We do not want to be exposing these? :(
#include "../../lib/Core/BranchTracker.h"
#include "klee/Internal/Module/KInstIterator.h"
#include "../../lib/Core/Memory.h"
#include <llvm/IR/Function.h>
#include "Internal/Module/KInstruction.h"
#include <llvm/IR/Instructions.h>

#include <map>
#include <set>
#include <vector>


namespace klee{
class ExecutionState;
class Cell;
class MemoryObject;
class KFunction;
class KInstIterator;
class CallPathNode;

struct StackFrame {
  friend class ExecutionState;
  unsigned		call;
  KInstIterator		caller;
  KFunction		*kf;
  CallPathNode		*callPathNode;
  KFunction		*onRet;
  ref<Expr>		onRet_expr;

//private:
  Cell *locals;
private:
  std::vector<const MemoryObject*> allocas;

public:
  /// Minimum distance to an uncovered instruction once the function
  /// returns. This is not a good place for this but is used to
  /// quickly compute the context sensitive minimum distance to an
  /// uncovered instruction. This value is updated by the StatsTracker
  /// periodically.
  unsigned minDistToUncoveredOnReturn;

  // For vararg functions: arguments not passed via parameter are
  // stored (packed tightly) in a local (alloca) memory object. This
  // is setup to match the way the front-end generates vaarg code (it
  // does not pass vaarg through as expected). VACopy is lowered inside
  // of intrinsic lowering.
  const MemoryObject *varargs;

  StackFrame(KInstIterator caller, KFunction *kf);
  StackFrame(const StackFrame &s);
  ~StackFrame();
  StackFrame& operator=(const StackFrame &s);
  void addAlloca(const MemoryObject*);
  bool clearLocals(void);
  bool isClear(void) const { return locals == NULL; }
};

}

#endif

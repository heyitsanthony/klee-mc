#include "ESESupport.h"
#include "llvm/Instructions.h"
#include "llvm/Instruction.h"
#include "klee/ExecutionState.h"
#include "llvm/Function.h"
#include "Sugar.h"
#include "StaticRecord.h"
#include <set>

using namespace klee;
using namespace llvm;

void ESESupport::checkControlDependenceStack(ExecutionState* state) {

  std::set<Function*> fns;

  foreach(it, state->stack.begin(), state->stack.end()) {
    const StackFrame& f = *it;
    fns.insert(f.kf->function);
  }

  foreach(it, state->controlDependenceStack.begin(), state->controlDependenceStack.end()) {
    StateRecord* rec = *it;
    Function* f = rec->staticRecord->function;
    if (!fns.count(f)) {
      std::cout << "INVALID CDS ENTRY: " << rec->staticRecord->function->getNameStr() << " " << rec->staticRecord->name() << std::endl;
      std::cout << "STACK" << std::endl;

      foreach(it, state->stack.begin(), state->stack.end()) {
        const StackFrame& f = *it;
        std::cout << " " << f.kf->function->getNameStr() << std::endl;
      }

      std::cout << "PC: " << *(state->pc->inst) << std::endl;

      assert(false);
    }
  }
}

bool ESESupport::isBBEntry(Instruction* inst) {
  BasicBlock* bb = inst->getParent();
  return &(bb->front()) == inst;
}

bool ESESupport::isRecStart(ExecutionState* state) {
  KInstruction* ki = state->pc;
  return (ki->inst == ki->staticRecord->insts.front());

}





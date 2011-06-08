#include "klee/Internal/Module/KModule.h"
#include "../lib/Core/Context.h"

#include "Passes.h"

#include "klee/Common.h"
#include "klee/Interpreter.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Support/ModuleUtil.h"

#include "llvm/Linker.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/ValueSymbolTable.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#if (LLVM_VERSION_MAJOR == 2 && LLVM_VERSION_MINOR == 6)
#include "llvm/Support/raw_os_ostream.h"
#endif
#include "llvm/System/Path.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Transforms/Scalar.h"

#include "static/Sugar.h"

using namespace klee;

KFunction::KFunction(llvm::Function *_function,
                     KModule *km)
  : function(_function),
    numArgs(function->arg_size()),
    numInstructions(0),
    callcount(0),
    trackCoverage(true)
{
  foreach (bbit, function->begin(), function->end()) {
    BasicBlock *bb = bbit;
    basicBlockEntry[bb] = numInstructions;
    numInstructions += bb->size();
  }

  instructions = new KInstruction*[numInstructions];
  arguments = new Value*[numArgs];

  std::map<Instruction*, unsigned> registerMap;

  unsigned c = 0;
  foreach (it, function->arg_begin(), function->arg_end()) {
        Value* v = &*it;
        arguments[c++] = v;
  }

  // The first arg_size() registers are reserved for formals.
  unsigned rnum = numArgs;
  foreach (bbit, function->begin(), function->end()) {
    foreach (it, bbit->begin(), bbit->end()) {
      registerMap[it] = rnum++;
    }
  }
  numRegisters = rnum;

  // build shadow instructions */
  unsigned i = 0;
  foreach (bbit, function->begin(), function->end()) {
    foreach(it, bbit->begin(), bbit->end()) {
      addInstruction(km, it, registerMap, i);
    }
  }
}

void KFunction::addInstruction(
	KModule	*km,
	llvm::Instruction* inst,
	std::map<llvm::Instruction*, unsigned>& registerMap,
	unsigned int& i)
{
      KInstruction *ki;

      switch(inst->getOpcode()) {
      case Instruction::GetElementPtr:
        ki = new KGEPInstruction(); break;
      default:
        ki = new KInstruction(); break;
      }

      unsigned numOperands = inst->getNumOperands();
      ki->inst = inst;
      ki->operands = new int[numOperands];
      ki->dest = registerMap[inst];
      for (unsigned j=0; j<numOperands; j++) {
        Value *v = inst->getOperand(j);

        if (Instruction *new_inst = dyn_cast<Instruction>(v)) {
          ki->operands[j] = registerMap[new_inst];
        } else if (Argument *a = dyn_cast<Argument>(v)) {
          ki->operands[j] = a->getArgNo();
        } else if (isa<BasicBlock>(v) || isa<InlineAsm>(v) ||
                   isa<MDNode>(v) /* || isa<Function>(v) */)  {
          ki->operands[j] = -1;
        } else {
          assert(isa<Constant>(v));
          Constant *c = cast<Constant>(v);
          ki->operands[j] = -(km->getConstantID(c, ki) + 2);
        }
      }

      instructions[i++] = ki;
}

llvm::Value* KFunction::getValueForRegister(unsigned reg) {
    if (reg < numArgs) {
        return arguments[reg];
    } else {
        return instructions[reg - numArgs]->inst;
    }
}

KFunction::~KFunction()
{
  delete[] arguments;
  for (unsigned i=0; i<numInstructions; ++i)
    delete instructions[i];
  delete[] instructions;
}



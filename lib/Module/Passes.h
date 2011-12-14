//===-- Passes.h ------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_PASSES_H
#define KLEE_PASSES_H

#include "llvm/BasicBlock.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/CodeGen/IntrinsicLowering.h"

namespace llvm {
  class Function;
  class Instruction;
  class Module;
  class TargetData;
  class Type;
  class IntrinsicInst;
  class TargetLowering;
  class TargetMachine;
}

namespace klee {
class KModule;

/// RaiseAsmPass - This pass raises some common occurences of inline
/// asm which are used by glibc into normal LLVM IR.
class RaiseAsmPass : public llvm::FunctionPass
{
private:
	static char ID;
	const llvm::TargetLowering *TLI;
	llvm::TargetMachine	*TM;

	llvm::Function *getIntrinsic(unsigned IID, llvm::Type *Ty0)
	{ return getIntrinsic(IID, Ty0); }

	bool runOnInstruction(llvm::Instruction *I);
	llvm::Module* module_;
public:
	RaiseAsmPass(llvm::Module* module);
	virtual ~RaiseAsmPass();
	virtual bool runOnFunction(llvm::Function& f);
};

/// RemoveSentinelsPass - This pass removes '\01' prefix sentinels added by
/// llvm-gcc when functions are renamed using __asm__
class RemoveSentinelsPass : public llvm::FunctionPass
{
  static char ID;
  virtual bool runOnFunction(llvm::Function &F);
public:
  RemoveSentinelsPass() : llvm::FunctionPass(ID) {}
};

  // This is a module pass because it can add and delete module
  // variables (via intrinsic lowering).
class IntrinsicCleanerPass : public llvm::FunctionPass
{
  static char ID;
  const llvm::TargetData &TargetData;
  llvm::IntrinsicLowering *IL;
  bool LowerIntrinsics;

  bool runOnBasicBlock(llvm::BasicBlock &b);
public:
  IntrinsicCleanerPass(
  	KModule* in_km, const llvm::TargetData &TD, bool LI=true)
    : llvm::FunctionPass((ID)),
      TargetData(TD),
      IL(new llvm::IntrinsicLowering(TD)),
      LowerIntrinsics(LI),
      km(in_km) {}
  ~IntrinsicCleanerPass() { delete IL; }

  virtual bool runOnFunction(llvm::Function& f);
private:
  void createReturnStruct(
	llvm::Type * retType, llvm::Value * value1,
	llvm::Value * value2, llvm::BasicBlock * bb);

  void clean_vacopy(llvm::BasicBlock::iterator& i, llvm::IntrinsicInst* ii);
  bool clean_dup_stoppoint(
  	llvm::BasicBlock::iterator& i, llvm::IntrinsicInst* ii);
  KModule* km;
};

// performs two transformations which make interpretation
// easier and faster.
//
// 1) Ensure that all the PHI nodes in a basic block have
//    the incoming block list in the same order. Thus the
//    incoming block index only needs to be computed once
//    for each transfer.
//
// 2) Ensure that no PHI node result is used as an argument to
//    a subsequent PHI node in the same basic block. This allows
//    the transfer to execute the instructions in order instead
//    of in two passes.
class PhiCleanerPass : public llvm::FunctionPass {
  static char ID;

public:
  PhiCleanerPass() : llvm::FunctionPass(ID) {}

  virtual bool runOnFunction(llvm::Function &f);
};

class DivCheckPass : public llvm::FunctionPass
{
  static char ID;
public:
  DivCheckPass(): FunctionPass(ID), divZeroCheckFunction(0) {}
  virtual bool runOnFunction(llvm::Function &f);
private:
  llvm::Function *divZeroCheckFunction;
};

/// LowerSwitchPass - Replace all SwitchInst instructions with chained branch
/// instructions.  Note that this cannot be a BasicBlock pass because it
/// modifies the CFG!
class LowerSwitchPass : public llvm::FunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  LowerSwitchPass() : FunctionPass(ID) {}

  virtual bool runOnFunction(llvm::Function &F);

  struct SwitchCase {
    llvm ::Constant *value;
    llvm::BasicBlock *block;

    SwitchCase() : value(0), block(0) { }
    SwitchCase(llvm::Constant *v, llvm::BasicBlock *b) :
      value(v), block(b) { }
  };

  typedef std::vector<SwitchCase>           CaseVector;
  typedef std::vector<SwitchCase>::iterator CaseItr;

private:
  void processSwitchInst(llvm::SwitchInst *SI);
  void switchConvert(CaseItr begin,
                     CaseItr end,
                     llvm::Value *value,
                     llvm::BasicBlock *origBlock,
                     llvm::BasicBlock *defaultBlock);
};

#if 0
/// LowerAtomic - get rid of llvm.atomic.xxx
class LowerAtomic : public llvm::BasicBlockPass {
public:
  static char ID;
  LowerAtomic() : BasicBlockPass(ID) {}
  bool runOnBasicBlock(llvm::BasicBlock &BB);
};
#endif
}

#endif

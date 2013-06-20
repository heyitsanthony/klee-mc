//===-- ExecutorUtil.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Executor.h"

#include "Context.h"

#include "static/Sugar.h"
#include "klee/Expr.h"
#include "klee/Interpreter.h"
#include "klee/Solver.h"

#include "klee/Internal/Module/KModule.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/GetElementPtrTypeIterator.h"
#include "llvm/IR/DataLayout.h"
#include <iostream>
#include <cassert>

using namespace klee;
using namespace llvm;

namespace klee {
class Globals;

  ref<klee::ConstantExpr>
  Executor::evalConstantExpr(
	const KModule* km,
	const Globals* gm,
	llvm::ConstantExpr *ce)
  {
    llvm::Type *type = ce->getType();

    ref<ConstantExpr> op[3];
    int numOperands = ce->getNumOperands();

    if (numOperands > 0) op[0] = evalConstant(km, gm, ce->getOperand(0));
    if (numOperands > 1) op[1] = evalConstant(km, gm, ce->getOperand(1));
    if (numOperands > 2) op[2] = evalConstant(km, gm, ce->getOperand(2));

    switch (ce->getOpcode()) {
    default :
      ce->dump();
      std::cerr << "error: unknown ConstantExpr type\n"
                << "opcode: " << ce->getOpcode() << "\n";
      for (unsigned i = 0; i < 3 && op[i].isNull() == false; i++) {
      	std::cerr << "CE[" << i << "]: " << op[i] << '\n';
      }

      abort();

    case Instruction::ShuffleVector: {
    	return cast<ConstantExpr>(instShuffleVectorEvaled(
		dyn_cast<VectorType>(ce->getType()),
		op[0], op[1], op[2]));
    }

    case Instruction::Trunc:
      return op[0]->Extract(0, km->getWidthForLLVMType(type));
    case Instruction::ZExt:  return op[0]->ZExt(km->getWidthForLLVMType(type));
    case Instruction::SExt:  return op[0]->SExt(km->getWidthForLLVMType(type));
    case Instruction::Add:   return op[0]->Add(op[1]);
    case Instruction::Sub:   return op[0]->Sub(op[1]);
    case Instruction::Mul:   return op[0]->Mul(op[1]);
    case Instruction::SDiv:  return op[0]->SDiv(op[1]);
    case Instruction::UDiv:  return op[0]->UDiv(op[1]);
    case Instruction::SRem:  return op[0]->SRem(op[1]);
    case Instruction::URem:  return op[0]->URem(op[1]);
    case Instruction::And:   return op[0]->And(op[1]);
    case Instruction::Or:    return op[0]->Or(op[1]);
    case Instruction::Xor:   return op[0]->Xor(op[1]);
    case Instruction::Shl:   return op[0]->Shl(op[1]);
    case Instruction::LShr:  return op[0]->LShr(op[1]);
    case Instruction::AShr:  return op[0]->AShr(op[1]);
    case Instruction::BitCast:  return op[0];

    case Instruction::IntToPtr:
      return op[0]->ZExt(km->getWidthForLLVMType(type));

    case Instruction::PtrToInt:
      return op[0]->ZExt(km->getWidthForLLVMType(type));

    case Instruction::GetElementPtr: {
      ref<ConstantExpr> base = op[0]->ZExt(Context::get().getPointerWidth());

      foreach (ii, gep_type_begin(ce), gep_type_end(ce)) {
        ref<ConstantExpr> addend =
          ConstantExpr::alloc(0, Context::get().getPointerWidth());

        if (StructType *st = dyn_cast<StructType>(*ii)) {
          const StructLayout *sl = km->dataLayout->getStructLayout(st);
          ConstantInt *ci = cast<ConstantInt>(ii.getOperand());

          addend = ConstantExpr::alloc(
	    sl->getElementOffset(
	      (unsigned)ci->getZExtValue()),
              Context::get().getPointerWidth());
        } else {
          SequentialType *seq_t = cast<SequentialType>(*ii);
          ref<ConstantExpr> index =
            evalConstant(km, gm, cast<Constant>(ii.getOperand()));
          unsigned elementSize = km->dataLayout->getTypeStoreSize(
	  	seq_t->getElementType());

          index = index->ZExt(Context::get().getPointerWidth());
          addend = index->Mul(ConstantExpr::alloc(
	    elementSize,
            Context::get().getPointerWidth()));
        }

        base = base->Add(addend);
      }

      return base;
    }

    case Instruction::ICmp: {
      switch(ce->getPredicate()) {
      default: assert(0 && "unhandled ICmp predicate");
      case ICmpInst::ICMP_EQ:  return op[0]->Eq(op[1]);
      case ICmpInst::ICMP_NE:  return op[0]->Ne(op[1]);
      case ICmpInst::ICMP_UGT: return op[0]->Ugt(op[1]);
      case ICmpInst::ICMP_UGE: return op[0]->Uge(op[1]);
      case ICmpInst::ICMP_ULT: return op[0]->Ult(op[1]);
      case ICmpInst::ICMP_ULE: return op[0]->Ule(op[1]);
      case ICmpInst::ICMP_SGT: return op[0]->Sgt(op[1]);
      case ICmpInst::ICMP_SGE: return op[0]->Sge(op[1]);
      case ICmpInst::ICMP_SLT: return op[0]->Slt(op[1]);
      case ICmpInst::ICMP_SLE: return op[0]->Sle(op[1]);
      }
    }

    case Instruction::Select: return op[0]->isTrue() ? op[1] : op[2];

    case Instruction::FAdd:
    case Instruction::FSub:
    case Instruction::FMul:
    case Instruction::FDiv:
    case Instruction::FRem:
    case Instruction::FPTrunc:
    case Instruction::FPExt:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
    case Instruction::FCmp:
      assert(0 && "floating point ConstantExprs unsupported");
    }
  }

}

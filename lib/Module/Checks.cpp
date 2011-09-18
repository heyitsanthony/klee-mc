//===-- Checks.cpp --------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Passes.h"

#include "llvm/LLVMContext.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/InstrTypes.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Type.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Target/TargetData.h"
#include "static/Sugar.h"

using namespace llvm;
using namespace klee;

char DivCheckPass::ID;

bool DivCheckPass::runOnFunction(Function& f)
{
	Module	*M;
	bool changed = false;

	M = f.getParent();
	assert (M != NULL && "Orphaned functoin on function pass");

	foreach (b, f.begin(), f.end()) {
	foreach (i, b->begin(), b->end()) {
		BinaryOperator		*binOp;
		CastInst		*denominator;
		Instruction::BinaryOps	opcode;

		binOp = dyn_cast<BinaryOperator>(i);
		if (binOp == NULL) continue;

		// find all [s|u][div|mod] instructions
		opcode = binOp->getOpcode();
		if (	opcode != Instruction::SDiv &&
			opcode != Instruction::UDiv &&
			opcode != Instruction::SRem &&
			opcode != Instruction::URem)
		{
			continue;
		}

		denominator = CastInst::CreateIntegerCast(
			i->getOperand(1),
			Type::getInt64Ty(getGlobalContext()),
			false,  /* sign doesn't matter */
			"int_cast_to_i64",
			i);

		// Lazily bind the function to avoid always importing it.
		if (!divZeroCheckFunction) {
			Constant *fc;
			fc = M->getOrInsertFunction(
				"klee_div_zero_check",
				Type::getVoidTy(getGlobalContext()),
				Type::getInt64Ty(getGlobalContext()),
				NULL);
			divZeroCheckFunction = cast<Function>(fc);
		}

		CallInst::Create(
			divZeroCheckFunction,
			denominator,
			"",
			&*i);
		changed = true;
	}
	}

	return changed;
}

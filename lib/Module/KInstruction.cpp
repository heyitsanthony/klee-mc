//===-- KInstruction.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Support/CallSite.h>
#include <llvm/Target/TargetData.h>
#include <string.h>

#include "../Core/Executor.h"
#include "../Core/Context.h"
#include "klee/Expr.h"
#include "klee/util/GetElementPtrTypeIterator.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"

using namespace llvm;
using namespace klee;

KInstruction::~KInstruction() { delete[] operands; }

KInstruction::KInstruction(Instruction* in_inst, unsigned in_dest)
: inst(in_inst)
, dest(in_dest)
, covered(false)
{
	if (isCall()) {
		/* [0] = getCalledValue() */
		/* [1...] = args */
		operands = new int[getNumArgs()+1];
		memset(operands, 0xff, sizeof(int)*(getNumArgs()+1));
		return;
	}

	operands = new int[getNumArgs()];
	memset(operands, 0xff, sizeof(int)*getNumArgs());
}

unsigned KInstruction::getNumArgs(void) const
{
	if (isCall()) {
		CallSite	cs(inst);
		return cs.arg_size();
	}

	return inst->getNumOperands();
}

bool KInstruction::isCall(void) const
{ return (isa<CallInst>(inst) || isa<InvokeInst>(inst)); }


KInstruction* KInstruction::create(
	KModule* km, llvm::Instruction* inst, unsigned dest)
{
	switch(inst->getOpcode()) {
	case Instruction::GetElementPtr:
	case Instruction::InsertValue:
	case Instruction::ExtractValue:
		return new KGEPInstruction(km, inst, dest);
	case Instruction::Br:
		return new KBrInstruction(inst, dest);
	default:
		return new KInstruction(inst, dest);
	}
}

KGEPInstruction::KGEPInstruction(
	KModule* km,
	llvm::Instruction* inst, unsigned dest)
: KInstruction(inst, dest)
, offset(~0)
{}

void KGEPInstruction::resolveConstants(const KModule* km, const Globals* g)
{
	if (GetElementPtrInst *gepi = dyn_cast<GetElementPtrInst>(getInst())) {
		computeOffsets(km, g, gep_type_begin(gepi), gep_type_end(gepi));
	} else if (InsertValueInst *ivi = dyn_cast<InsertValueInst>(getInst())) {
		computeOffsets(km, g, iv_type_begin(ivi), iv_type_end(ivi));
		assert(	indices.empty() &&
			"InsertValue constant offset expected");
	} else if (ExtractValueInst *evi = dyn_cast<ExtractValueInst>(getInst())) {
		computeOffsets(km, g, ev_type_begin(evi), ev_type_end(evi));
		assert(	indices.empty() &&
			"ExtractValue constant offset expected");
	} else {
		assert (0 == 1 && "UNKNOWN GEPI");
	}
}

template <typename TypeIt>
void KGEPInstruction::computeOffsets(
	const KModule* km, const Globals* g, TypeIt ib, TypeIt ie)
{
	ref<ConstantExpr>	constantOffset;
	uint64_t		index;

	indices.clear();

	constantOffset = ConstantExpr::alloc(0, Context::get().getPointerWidth());

	index = 1;
	for (TypeIt ii = ib; ii != ie; ++ii) {
	if (StructType *st = dyn_cast<StructType>(*ii)) {
		const StructLayout	*sl;
		const ConstantInt	*ci;
		uint64_t		addend;

		sl = km->targetData->getStructLayout(st);
		ci = cast<ConstantInt>(ii.getOperand());
		addend = sl->getElementOffset((unsigned) ci->getZExtValue());

		constantOffset = constantOffset->Add(
			ConstantExpr::alloc(
				addend, Context::get().getPointerWidth()));
	} else {
		const SequentialType	*st2;
		uint64_t		elementSize;
		Value			*operand;

		st2 = cast<SequentialType>(*ii);
		elementSize = km->targetData->getTypeStoreSize(
			st2->getElementType());
		operand = ii.getOperand();

		if (Constant *c = dyn_cast<Constant>(operand)) {
			ref<ConstantExpr>	cVal;
			ref<ConstantExpr>	index;
			ref<ConstantExpr>	addend;

			cVal = Executor::evalConstant(km, g, c);

			index = cast<ConstantExpr>(cVal)->SExt(
				Context::get().getPointerWidth());

			addend = index->Mul(
				ConstantExpr::alloc(
					elementSize,
					Context::get().getPointerWidth()));
			constantOffset = constantOffset->Add(addend);

		} else {
			indices.push_back(std::make_pair(index, elementSize));
		}
	}
	index++;
	}

	offset = constantOffset->getZExtValue();
}



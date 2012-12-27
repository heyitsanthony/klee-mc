//===-- IntrinsicCleaner.cpp ----------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Passes.h"

#include <llvm/LLVMContext.h>
#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Function.h>
#include <llvm/InstrTypes.h>
#include <llvm/Instruction.h>
#include <llvm/Instructions.h>
#include <llvm/IntrinsicInst.h>
#include <llvm/Module.h>
#include <llvm/Pass.h>
#include <llvm/Type.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/DataLayout.h>
#include "klee/Internal/Module/KModule.h"
#include <iostream>
#include <sstream>

#include "static/Sugar.h"
using namespace llvm;

namespace klee {

char IntrinsicCleanerPass::ID;

bool IntrinsicCleanerPass::runOnFunction(llvm::Function& f)
{
	bool dirty = false;
	foreach(b, f.begin(), f.end()) {
		dirty |= runOnBasicBlock(*b);
	}
	return dirty;
}

//
// FIXME: This is much more target dependent than just the word size,
// however this works for x86-32 and x86-64.
// (dst, src) -> *((i8**) dst) = *((i8**) src)
void IntrinsicCleanerPass::clean_vacopy(
	BasicBlock::iterator& i,
	IntrinsicInst* ii)
{
	Value *dst = ii->getOperand(0);
	Value *src = ii->getOperand(1);

	unsigned WordSize = DataLayout.getPointerSizeInBits() / 8;

	if (WordSize == 4) {
		Type	*i8pp;
		Value	*castedDst, *castedSrc, *load;
		i8pp = PointerType::getUnqual(
			PointerType::getUnqual(
				Type::getInt8Ty(getGlobalContext())));

		castedDst = CastInst::CreatePointerCast(
			dst, i8pp, "vacopy.cast.dst", ii);
		castedSrc = CastInst::CreatePointerCast(
			src, i8pp, "vacopy.cast.src", ii);

		load = new LoadInst(castedSrc, "vacopy.read", ii);
		new StoreInst(load, castedDst, false, ii);
	} else {
		assert(WordSize == 8 && "Invalid word size!");
		Type	*i64p;
		Value	*pDst, *pSrc, *val, *off;

		i64p = PointerType::getUnqual(
			Type::getInt64Ty(getGlobalContext()));
		pDst = CastInst::CreatePointerCast(
			dst, i64p, "vacopy.cast.dst", ii);
		pSrc = CastInst::CreatePointerCast(
			src, i64p, "vacopy.cast.src", ii);
		val = new LoadInst(pSrc, std::string(), ii);
		new StoreInst(val, pDst, ii);

		off = ConstantInt::get(Type::getInt64Ty(getGlobalContext()), 1);
		pDst = GetElementPtrInst::Create(pDst, off, std::string(), ii);
		pSrc = GetElementPtrInst::Create(pSrc, off, std::string(), ii);
		val = new LoadInst(pSrc, std::string(), ii);
		new StoreInst(val, pDst, ii);

		pDst = GetElementPtrInst::Create(pDst, off, std::string(), ii);
		pSrc = GetElementPtrInst::Create(pSrc, off, std::string(), ii);
		val = new LoadInst(pSrc, std::string(), ii);
		new StoreInst(val, pDst, ii);
	}

	ii->removeFromParent();
	delete ii;
}

// We can remove this stoppoint if the next instruction is
// sure to be another stoppoint. This is nice for cleanliness
// but also important for switch statements where it can allow
// the targets to be joined.
bool IntrinsicCleanerPass::clean_dup_stoppoint(
	llvm::BasicBlock::iterator& i,
	llvm::IntrinsicInst* ii)
{
	if (isa<UnreachableInst>(i)) {
		ii->eraseFromParent();
		return true;
	}

	return false;
}

void IntrinsicCleanerPass::createReturnStruct(
	llvm::Type * retType,
	Value * value1, Value * value2, BasicBlock * bb)
{
	std::vector<Value *>	indicies1(2), indicies2(2);
	Value			*retVal, *address1, *address2, *finalRet;

	retVal = new AllocaInst(retType,"",bb);

	indicies1[0] = ConstantInt::get(Type::getInt32Ty(getGlobalContext()),0);
	indicies1[1] = ConstantInt::get(Type::getInt32Ty(getGlobalContext()),0);
	address1 = GetElementPtrInst::CreateInBounds(retVal, indicies1,"",bb);

	indicies2[0] = ConstantInt::get(Type::getInt32Ty(getGlobalContext()),0);
	indicies2[1] = ConstantInt::get(Type::getInt32Ty(getGlobalContext()),1);
	address2 = GetElementPtrInst::CreateInBounds(retVal, indicies2,"",bb);

	new StoreInst(value1,address1,bb);
	new StoreInst(value2,address2,bb);

	finalRet = new LoadInst(retVal,"",bb);
	ReturnInst::Create(getGlobalContext(),finalRet,bb);
}


void IntrinsicCleanerPass::instUMulWithOF(
	llvm::BasicBlock& b, llvm::IntrinsicInst* ii)
{
	Function::ArgumentListType::iterator ait;
	Argument *arg_a, *arg_b;
        Module * m = b.getParent()->getParent();
	BasicBlock *bb;
	Value		*Result, *aq, *ICMPDiff;
	Type		*retType;
        std::stringstream namestream;

        namestream << "_umul_with_overflow_";
        namestream << cast<StructType>(ii->getType())->getElementType(0)->getPrimitiveSizeInBits();
        namestream << "bit_impl";
        const std::string & newname = namestream.str();
        Function * f = m->getFunction(newname);
        if (f != NULL) goto done;

	f = Function::Create(
		ii->getCalledFunction()->getFunctionType(),
		GlobalValue::ExternalLinkage,
		newname,m);
	ait = f->getArgumentList().begin();

	arg_a = &*ait;
	arg_b = &*++ait;
	arg_a->setName("a");
	arg_b->setName("b");
	bb = BasicBlock::Create(getGlobalContext(),"entry",f);
	Result = BinaryOperator::CreateMul(arg_a, arg_b,"",bb);
	aq = BinaryOperator::CreateUDiv(Result, arg_b,"",bb);
	ICMPDiff = new ICmpInst(*bb,CmpInst::ICMP_NE,arg_a,aq);

	retType = f->getReturnType();
	createReturnStruct(retType,Result,ICMPDiff,bb);

done:
        ii->getCalledFunction()->replaceAllUsesWith(f);
}

void IntrinsicCleanerPass::instUAddWithOF(
	BasicBlock& b,
	IntrinsicInst* ii)
{
	Module 		*m;
	Function	*f;
	BasicBlock	*bb;
	Argument	*arg_a, *arg_b;
	Value		*Result, *ICMPLarger, *Larger, *Overflow;
	Type		*retType;
	StructType	*st;
	Function::ArgumentListType::iterator ait;
	std::stringstream namestream;

	st = cast<StructType>(ii->getType());

	namestream << "_uadd_with_overflow_"
		<< st->getElementType(0)->getPrimitiveSizeInBits()
		<< "bit_impl";

	const std::string & newname = namestream.str();

	m = b.getParent()->getParent();
	f = m->getFunction(newname);
	if (f != NULL) goto done;

	f = Function::Create(
		ii->getCalledFunction()->getFunctionType(),
		GlobalValue::ExternalLinkage,
		newname,
		m);
	ait = f->getArgumentList().begin();
	arg_a = &*ait;
	arg_b = &*++ait;
	arg_a->setName("a");
	arg_b->setName("b");
	bb = BasicBlock::Create(getGlobalContext(),"entry",f);
	Result = BinaryOperator::CreateAdd(arg_a, arg_b,"",bb);
	ICMPLarger = new ICmpInst(*bb,CmpInst::ICMP_UGE,arg_a,arg_b);
	Larger = SelectInst::Create(ICMPLarger, arg_a, arg_b, "", bb);
	Overflow = new ICmpInst(*bb, CmpInst::ICMP_UGT, Larger, Result);

	retType = f->getReturnType();
	createReturnStruct(retType,Result,Overflow,bb);

	km->addFunctionProcessed(f);

done:
	ii->getCalledFunction()->replaceAllUsesWith(f);
}



bool IntrinsicCleanerPass::runOnBasicBlock(BasicBlock &b)
{
	bool dirty = false;

	for (BasicBlock::iterator i = b.begin(), ie = b.end(); i != ie;) {
		IntrinsicInst *ii = dyn_cast<IntrinsicInst>(&*i);

		// increment now; LowerIntrinsic delete makes iterator invalid.
		++i;
		if (!ii) {
			if (i->getOpcode() == Instruction::Fence) {
				i->eraseFromParent();
				dirty = true;
			}
			continue;
		}

		switch (ii->getIntrinsicID()) {
		case Intrinsic::vastart:
		case Intrinsic::vaend:
			break;

		/* stolen from Ben. So fucking sick of llvm-3.0 right now */
		case Intrinsic::uadd_with_overflow:
			instUAddWithOF(b, ii);
			dirty = true;
			break;
		case Intrinsic::umul_with_overflow:
			instUMulWithOF(b, ii);
			dirty = true;
			break;

		// Lower vacopy so that object resolution etc is handled by
		// normal instructions.
		case Intrinsic::vacopy:
			clean_vacopy(i, ii);
			break;

		case Intrinsic::dbg_declare:
		case Intrinsic::dbg_value:
			ii->eraseFromParent();
			dirty = true;
			break;

//		case Intrinsic::dbg_stoppoint:
#if 0
		case Intrinsic::dbg_region_start:
		case Intrinsic::dbg_region_end:
		case Intrinsic::dbg_func_start:
			// Remove these regardless of lower intrinsics flag.
			// This can be removed once IntrinsicLowering is fixed to
			// not have bad caches.
#endif
		default:
			if (LowerIntrinsics)
				IL->LowerIntrinsicCall(ii);
			dirty = true;
			break;
		}
	}

	return dirty;
}

}

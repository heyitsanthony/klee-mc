//===-- RaiseAsm.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Passes.h"
#include "static/Sugar.h"
#include "llvm/LLVMContext.h"
#include "llvm/InlineAsm.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetLowering.h"
#include "llvm/Support/Host.h"
#include "llvm/Target/TargetRegistry.h"

using namespace llvm;
using namespace klee;

char RaiseAsmPass::ID = 0;

RaiseAsmPass::RaiseAsmPass(llvm::Module* module)
: llvm::FunctionPass(ID)
, module_(module)
{
	std::string Err;
	std::string HostTriple = llvm::sys::getHostTriple();
	const Target *NativeTarget;
	NativeTarget = TargetRegistry::lookupTarget(HostTriple, Err);
	if (NativeTarget == 0) {
		llvm::errs()
			<< "Warning: unable to select native target: "
			<< Err << "\n";
		TLI = 0;
	} else {
		llvm::errs() << "HAVE NATIVE TARGET\n";
		TargetMachine *TM;
		TM = NativeTarget->createTargetMachine(HostTriple, "");
		TLI = TM->getTargetLowering();
	}
}

Function* RaiseAsmPass::getIntrinsic(
	unsigned IID,
	const Type **Tys,
	unsigned NumTys)
{
  return Intrinsic::getDeclaration(
  	module_, (llvm::Intrinsic::ID) IID, Tys, NumTys);
}

// FIXME: This should just be implemented as a patch to
// X86TargetAsmInfo.cpp, then everyone will benefit.
bool RaiseAsmPass::runOnInstruction(Instruction *I)
{
	CallInst	*ci;
	InlineAsm	*ia;

	if(!(ci = dyn_cast<CallInst>(I)))
		return false;

	if (!(ia = dyn_cast<InlineAsm>(ci->getCalledValue())))
		return false;

	const std::string &as = ia->getAsmString();
	const std::string &cs = ia->getConstraintString();
	const llvm::Type *T = ci->getType();

	// bswaps
	unsigned NumOperands = ci->getNumArgOperands() + 1;
	llvm::Value *Arg0 = NumOperands > 1 ? ci->getArgOperand(0) : 0;
	if (Arg0 && T == Arg0->getType() &&
		((T == llvm::Type::getInt16Ty(getGlobalContext()) &&
		as == "rorw $$8, ${0:w}" &&
		cs == "=r,0,~{dirflag},~{fpsr},~{flags},~{cc}") ||
		(T == llvm::Type::getInt32Ty(getGlobalContext()) &&
		as == "rorw $$8, ${0:w};rorl $$16, $0;rorw $$8, ${0:w}" &&
		cs == "=r,0,~{dirflag},~{fpsr},~{flags},~{cc}")))
	{
		Function *F = getIntrinsic(Intrinsic::bswap, Arg0->getType());
		ci->setCalledFunction(F);
		return true;
	}

	llvm::errs() << ia->getAsmString() << '\n';

	return TLI && TLI->ExpandInlineAsm(ci);
}

bool RaiseAsmPass::runOnFunction(Function& F)
{
	bool changed = false;
	foreach (bi, F.begin(), F.end()) {
		foreach (ii, bi->begin(), bi->end()) {
			Instruction *i = ii;
			changed |= runOnInstruction(i);
		}
	}
	return changed;
}

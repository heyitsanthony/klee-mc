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
#include "llvm/InlineAsm.h"

using namespace llvm;
using namespace klee;

char RaiseAsmPass::ID = 0;

Function* RaiseAsmPass::getIntrinsic(
	unsigned IID,
	const Type **Tys,
	unsigned NumTys)
{
  return Intrinsic::getDeclaration(module_, (llvm::Intrinsic::ID) IID, Tys, NumTys);
}

// FIXME: This should just be implemented as a patch to
// X86TargetAsmInfo.cpp, then everyone will benefit.
bool RaiseAsmPass::runOnInstruction(Instruction *I)
{
  CallInst *ci = dyn_cast<CallInst>(I);

  if (!ci) return false;

  InlineAsm *ia = dyn_cast<InlineAsm>(ci->getCalledValue());
  if (!ia) return false;

  const std::string &as = ia->getAsmString();
  const std::string &cs = ia->getConstraintString();
  const llvm::Type *T = ci->getType();

  // bswaps
  if (ci->getNumOperands() == 2 &&
      T == ci->getOperand(1)->getType() &&
      ((T == llvm::Type::getInt16Ty(getGlobalContext()) &&
        as == "rorw $$8, ${0:w}" &&
        cs == "=r,0,~{dirflag},~{fpsr},~{flags},~{cc}") ||
       (T == llvm::Type::getInt32Ty(getGlobalContext()) &&
        as == "rorw $$8, ${0:w};rorl $$16, $0;rorw $$8, ${0:w}" &&
        cs == "=r,0,~{dirflag},~{fpsr},~{flags},~{cc}"))) {
    llvm::Value *Arg0 = ci->getOperand(1);
    Function *F = getIntrinsic(Intrinsic::bswap, Arg0->getType());
    ci->setOperand(0, F);
    return true;
  }

  return false;
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

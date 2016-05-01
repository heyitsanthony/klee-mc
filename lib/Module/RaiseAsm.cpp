//===-- RaiseAsm.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetLowering.h>
#include <llvm/Target/TargetSubtargetInfo.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/CodeGen/IntrinsicLowering.h>

#include "Passes.h"
#include "static/Sugar.h"

using namespace llvm;
using namespace klee;

char RaiseAsmPass::ID = 0;

#include <iostream>

RaiseAsmPass::RaiseAsmPass(llvm::Module* module)
: llvm::FunctionPass(ID)
, TM(NULL)
{
	std::string	err;
	const Target	*NativeTarget;
	Triple		foundTriple;
	TargetOptions	to;

	NativeTarget = TargetRegistry::lookupTarget(
		"x86-64",
		foundTriple,
		err);
	if (NativeTarget == NULL) {
		llvm::errs()
			<< "Warning: unable to select native target: "
			<< err << "\n";
		TLI = 0;
		return;
	}
	TM = NativeTarget->createTargetMachine(foundTriple.str(), "", "", to);
	TLI = TM->getSubtargetImpl(*(module->begin()))->getTargetLowering();
}

RaiseAsmPass::~RaiseAsmPass(void) { if (TM) delete TM; }

#include <iostream>
// FIXME: This should just be implemented as a patch to
// X86TargetAsmInfo.cpp, then everyone will benefit.
bool RaiseAsmPass::runOnInstruction(Instruction *I)
{
	CallInst	*ci;
	InlineAsm	*ia;

	assert (I != NULL);

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
		cs == "=r,0,~{dirflag},~{fpsr},~{flags},~{cc}") ||

		(T == llvm::Type::getInt32Ty(getGlobalContext()) &&
		as == "bswap $0" &&
		cs == "=r,0,~{dirflag},~{fpsr},~{flags}"))
		)
	{
		return IntrinsicLowering::LowerToByteSwap(ci);
	}

	// llvm::errs() << ia->getAsmString() << '\n';
	return TLI && TLI->ExpandInlineAsm(ci);
}

bool RaiseAsmPass::runOnFunction(Function& F)
{
	bool changed = false;
	for (auto& bb : F) {
		for (auto ii = bb.begin(), ie = bb.end(); ii != ie;) {
			Instruction &i = const_cast<Instruction&>(*ii);
			++ii;
			changed |= runOnInstruction(&i);
		}
	}
	return changed;
}

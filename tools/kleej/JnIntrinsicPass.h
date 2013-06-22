#ifndef JNINTRINSICPASS_H
#define JNINTRINSICPASS_H

#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Pass.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Constants.h>
#include "static/Sugar.h"

#include <assert.h>
#include "klee/Internal/Module/KModule.h"

using namespace klee;
using namespace llvm;

class JnIntrinsicPass : public llvm::FunctionPass
{
private:
	static char		ID;

	llvm::CallInst* intr2call(IntrinsicInst* ii, const char* fname)
	{
		std::vector<llvm::Value*>	args;
		std::vector<Type *>		argTypes;
		Type				*retType;

		for (unsigned i = 0; i < ii->getNumArgOperands(); i++) {
			args.push_back(ii->getArgOperand(i));
			argTypes.push_back(args.back()->getType());
		}
		retType = ii->getType();

		return CallInst::Create(
			mod->getOrInsertFunction(
				fname,
				FunctionType::get(retType, argTypes, false)),
			ArrayRef<Value*>(args.data(), args.size()));
	}

	bool runOnBasicBlock(llvm::BasicBlock& bi)
	{
		unsigned	update_c = 0;

		foreach_manual (iit, bi.begin(), bi.end()) {
			IntrinsicInst *ii(dyn_cast<IntrinsicInst>(&*iit));

			++iit;

			if (ii == NULL)
				continue;

			switch (ii->getIntrinsicID()) {
			case Intrinsic::frameaddress:
				ReplaceInstWithInst(
					ii,
					intr2call(ii, "llvm_frameaddress"));
				update_c++;
				break;
			case Intrinsic::gcroot:
				ReplaceInstWithInst(
					ii,
					intr2call(ii, "llvm_gcroot"));
				update_c++;
				break;
			default:
				ii->dump();
				break;
			}
		}

		return (update_c != 0);
	}

	llvm::Module	*mod;
public:
	JnIntrinsicPass(llvm::Module* m)
	: llvm::FunctionPass(ID)
	, mod(m) {}
	virtual ~JnIntrinsicPass() {}
	virtual bool runOnFunction(llvm::Function& f)
	{
		bool was_changed = false;

		if (strncmp(f.getName().str().c_str(), "JnJVM", 5) != 0)
			return false;

		foreach (bit, f.begin(), f.end()) {
			if (runOnBasicBlock(*bit))
				was_changed = true;
		}

		return was_changed;
	}
};

#endif

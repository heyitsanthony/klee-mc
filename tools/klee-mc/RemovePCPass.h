#ifndef REMOVEPCPASS_H
#define REMOVEPCPASS_H

#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Pass.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include "static/Sugar.h"

#include <assert.h>
#include "klee/Internal/Module/KModule.h"

using namespace klee;
using namespace llvm;

class RemovePCPass : public llvm::FunctionPass
{
private:
	static char		ID;
	bool runOnBasicBlock(llvm::BasicBlock& bi)
	{
		unsigned	update_c = 0;
		Instruction	*last_pc_store = NULL;

		foreach_manual (iit, bi.begin(), bi.end()) {
			Instruction	*ii = iit;

			++iit;

			if (ii->getOpcode() != Instruction::Store)
				continue;

			/* XXX: get working on other archs */
			if (strncmp(
				ii->getOperand(1)->getName().str().c_str(),
				"RIP", 3))
			{
				continue;
			}

			/* if this instruction covers prior store inst to PC,
			 * prior instruction can safely be removed */
			if (last_pc_store != NULL) {
				last_pc_store->eraseFromParent();
				update_c++;
			}

			last_pc_store = ii;
		}

		return (update_c != 0);
	}

public:
	RemovePCPass() : llvm::FunctionPass(ID) {}
	virtual ~RemovePCPass() {}
	virtual bool runOnFunction(llvm::Function& f)
	{
		bool was_changed = false;

		if (strncmp(f.getName().str().c_str(), "sb_0x", 5) != 0)
			return false;

		foreach (bit, f.begin(), f.end()) {
			if (runOnBasicBlock(*bit))
				was_changed = true;
		}

		return was_changed;
	}
};

#endif

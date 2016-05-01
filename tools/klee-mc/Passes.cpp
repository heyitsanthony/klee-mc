#include <llvm/IR/CallSite.h>
#include "ExecutorVex.h"
#include "Passes.h"

char RemovePCPass::ID;
char OutcallMCPass::ID;

bool OutcallMCPass::runOnBasicBlock(llvm::BasicBlock& bi)
{
	unsigned	update_c = 0;

	foreach_manual (iit, bi.begin(), bi.end()) {
		const Instruction	*ii = &(*iit);
		Function	*f;
		std::string	fname;
		++iit;

		if (ii->getOpcode() != Instruction::Call)
			continue;

		auto ci = dyn_cast<const CallInst>(ii);
		CallSite	cs(const_cast<CallInst*>(ci));

		/* function already linked? */
		f = cs.getCalledFunction();
		if (f == NULL) continue;
		
		fname = f->getName().str();
		if (fname.substr(0, 6) == "__mc__")
			std::cerr << "HEY: " << fname << '\n';

		if (f->isDeclaration())
			std::cerr << "func name: " << fname << '\n';

	}

	return (update_c != 0);
}

bool OutcallMCPass::runOnFunction(llvm::Function& f)
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

bool RemovePCPass::runOnBasicBlock(llvm::BasicBlock& bi)
{
	unsigned	update_c = 0;
	Instruction	*last_pc_store = NULL;

	foreach_manual (iit, bi.begin(), bi.end()) {
		Instruction	&ii = *iit;

		++iit;

		if (ii.getOpcode() != Instruction::Store)
			continue;

		/* XXX: get working on other archs */
		if (strncmp(
			ii.getOperand(1)->getName().str().c_str(),
			"RIP", 3))
		{
			continue;
		}

		/* if this instruction covers prior store inst to PC,
		 * prior instruction can safely be removed */
		if (last_pc_store != nullptr) {
			last_pc_store->eraseFromParent();
			update_c++;
		}

		last_pc_store = &ii;
	}

	return (update_c != 0);
}

bool RemovePCPass::runOnFunction(llvm::Function& f)
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


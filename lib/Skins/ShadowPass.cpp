#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/LLVMContext.h>
#include <llvm/Instructions.h>
#include <llvm/Constants.h>
#include "klee/Internal/Module/KModule.h"
#include "static/Sugar.h"
#include "../Core/Executor.h"
#include "ShadowPass.h"

using namespace klee;
using namespace llvm;

char ShadowPass::ID;

/* instrument all stores to taint data */
bool ShadowPass::runOnBasicBlockStores(llvm::BasicBlock& bi, uint64_t tag)
{
	unsigned tainted_ins_c = 0;

	foreach_manual(iit, bi.begin(), bi.end()) {
		Instruction	*ii = iit;

		++iit;
		if (ii->getOpcode() != Instruction::Store)
			continue;

		CallInst *new_call;
		std::vector<Value*>	args;

		args.push_back(ii->getOperand(0));
		args.push_back(ii->getOperand(1));
		args.push_back(
			ConstantInt::get(
				IntegerType::get(getGlobalContext(), 64),
				tag));
		new_call = CallInst::Create(f_store, args);
		ReplaceInstWithInst(ii, new_call);
		tainted_ins_c++;
	}

	return (tainted_ins_c != 0);
}

/* instrument all loads to taint data */
bool ShadowPass::runOnBasicBlockLoads(llvm::BasicBlock& bi, uint64_t tag)
{
	Instruction	*last_load = NULL;
	unsigned	tainted_ins_c = 0;

	foreach_manual (iit, bi.begin(), bi.end()) {
		Instruction	*ii = iit;

		++iit;

	/* instrument post-load */
		if (last_load != NULL) {
			CallInst *new_call;
			std::vector<Value*>	args;

			args.push_back(
				ConstantInt::get(
					IntegerType::get(getGlobalContext(), 64),
					tag));
			new_call = CallInst::Create(f_load, args, "", ii);
			last_load = NULL;
			tainted_ins_c++;
		}

		if (ii->getOpcode() == Instruction::Load)
			last_load = ii;
	}

	return (tainted_ins_c != 0);
}

bool ShadowPass::isShadowedFunc(const llvm::Function& f) const
{ return isMMUFunc(f) || (getTagIter(f) != tags.end()); }

uint64_t ShadowPass::getTag(const llvm::Function& f) const
{
	if (isMMUFunc(f)) return 123456789;
	shadow_tags_ty::const_iterator it(getTagIter(f));
	assert (it != tags.end());
	return it->second;
}

bool ShadowPass::isMMUFunc(const llvm::Function& f) const
{
	if (strncmp("mmu_", f.getName().str().c_str(), 4) == 0)
		return true;
	return false;
}

shadow_tags_ty::const_iterator ShadowPass::getTagIter(
	const llvm::Function& f) const
{
	std::string			f_name_raw;
	std::string			f_name;
	shadow_tags_ty::const_iterator	it;
	unsigned			i;

	f_name_raw = exe.getKModule()->getPrettyName(&f);

	for (i = 0; f_name_raw[i] && f_name_raw[i] != '+'; i++);
	f_name = f_name_raw.substr(0, i);

	return tags.find(f_name);
}

bool ShadowPass::runOnFunction(llvm::Function& f)
{
	uint64_t	shadow_tag;
	bool		was_changed = false;

	if (!isShadowedFunc(f)) return false;

	shadow_tag = getTag(f);
	
	foreach (bit, f.begin(), f.end()) {
		if (runOnBasicBlockStores(*bit, shadow_tag))
			was_changed = true;
		if (runOnBasicBlockLoads(*bit, shadow_tag))
			was_changed = true;
	}

//	if (was_changed) f.dump();

	return was_changed;
}

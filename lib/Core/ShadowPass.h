#ifndef SHADOWPASS_H
#define SHADOWPASS_H

#include <llvm/Pass.h>
#include "ShadowCore.h"

namespace llvm
{
class Function;
class BasicBlock;
}

namespace klee
{
class Executor;

class ShadowPass : public llvm::FunctionPass
{
private:
	static char		ID;
	Executor		&exe;
	const shadow_tags_ty	&tags;
	llvm::Constant		*f_load, *f_store;

	bool runOnBasicBlockLoads(llvm::BasicBlock& bi, uint64_t tag);
	bool runOnBasicBlockStores(llvm::BasicBlock& bi, uint64_t tag);

	bool isShadowedFunc(const llvm::Function& f) const;
	uint64_t getTag(const llvm::Function& f) const;

	shadow_tags_ty::const_iterator getTagIter(
		const llvm::Function& f) const;

	bool isMMUFunc(const llvm::Function& f) const;
public:
	ShadowPass(
		Executor& _exe,
		llvm::Constant *_f_load,
		llvm::Constant *_f_store,
		const shadow_tags_ty& st)
	: llvm::FunctionPass(ID)
	, exe(_exe)
	, tags(st)
	, f_load(_f_load), f_store(_f_store) {}

	virtual ~ShadowPass() {}
	virtual bool runOnFunction(llvm::Function& f);
};
}

#endif

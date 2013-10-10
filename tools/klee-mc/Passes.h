#ifndef KLEEMCPASSES_H
#define KLEEMCPASSES_H


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
	bool runOnBasicBlock(llvm::BasicBlock& bi);
public:
	RemovePCPass() : llvm::FunctionPass(ID) {}
	virtual ~RemovePCPass() {}
	virtual bool runOnFunction(llvm::Function& f);
};

class OutcallMCPass : public llvm::FunctionPass
{
private:
	static char		ID;
	bool runOnBasicBlock(llvm::BasicBlock& bi);
public:
	OutcallMCPass() : llvm::FunctionPass(ID) {}
	virtual ~OutcallMCPass() {}
	virtual bool runOnFunction(llvm::Function& f);
};


#endif

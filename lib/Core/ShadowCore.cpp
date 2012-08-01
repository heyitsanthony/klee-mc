#include <fstream>
#include <iostream>
#include <llvm/Support/CommandLine.h>
#include <llvm/Pass.h>
#include <llvm/Module.h>
#include <llvm/LLVMContext.h>
#include <llvm/Type.h>
#include <llvm/Instructions.h>
#include <llvm/Constants.h>
#include "static/Sugar.h"

#include "../Expr/ShadowAlloc.h"
#include "../Expr/ShadowBuilder.h"

#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KModule.h"
#include "SpecialFunctionHandler.h"
#include "Executor.h"
#include "ShadowCore.h"

namespace llvm
{
cl::opt<std::string> ShadowFuncFile("shadow-func-file", cl::init("shadow.txt"));
}

using namespace klee;
using namespace llvm;

SFH_HANDLER(TaintOn)
SFH_HANDLER(TaintOff)

static struct SpecialFunctionHandler::HandlerInfo shadow_hi[] =
{
{
	"shadow_taint_on", 
	&HandlerTaintOn::create,
	false, /* terminates */
	false, /* has return value */
	false /* do not override intrinsic */
},
{ "shadow_taint_off", &HandlerTaintOff::create, false, false, false }
};

class ShadowPass : public llvm::FunctionPass
{
private:
	static char		ID;
	Executor		&exe;
	const shadow_tags_ty	&tags;
	llvm::Constant		*f_enter, *f_leave;

	bool runOnInstruction(llvm::Instruction *I);
public:
	ShadowPass(
		Executor& _exe,
		llvm::Constant *enter,
		llvm::Constant *leave,
		const shadow_tags_ty& st)
	: llvm::FunctionPass(ID)
	, exe(_exe)
	, tags(st)
	, f_enter(enter), f_leave(leave) {}

	virtual ~ShadowPass() {}
	virtual bool runOnFunction(llvm::Function& f);
};
char ShadowPass::ID;

bool ShadowPass::runOnFunction(llvm::Function& f)
{
	std::string			f_name(exe.getPrettyName(&f));
	shadow_tags_ty::const_iterator		it;
	unsigned				i;

	for (i = 0; f_name[i] && f_name[i] != '+'; i++);
	f_name = f_name.substr(0, i);
	
	it = tags.find(f_name);
	if (it == tags.end())
		return false;

	std::cerr << "PARSED: " << f_name << '\n';


	/* instrument all loads to taint data */
	foreach (bit, f.begin(), f.end()) {
		foreach (iit, bit->begin(), bit->end()) {
			Instruction		*ii = iit;
			std::vector<Value*>	args;

			if (ii->getOpcode() != Instruction::Load)
				continue;

			CallInst *new_call;
			args.push_back(
				ConstantInt::get(
					IntegerType::get(getGlobalContext(), 32),
					it->second));
			new_call = CallInst::Create(
				f_enter,
				args,
				"hook",
				ii);

		}
	}
	f.dump();
	assert (0 == 1);
	return true;
}


ShadowCore::ShadowCore(Executor* _exe)
: exe(_exe)
{
	ExprAlloc	*ea;

	loadShadowTags(ShadowFuncFile);
	assert (shadow_tags.size() != 0);

	ea = Expr::getAllocator();
	Expr::setAllocator(new ShadowAlloc());
	delete ea;
	Expr::setBuilder(ShadowBuilder::create(Expr::getBuilder()));
}

void ShadowCore::loadShadowTags(const std::string& s)
{
	std::ifstream	ifs(s.c_str());
	std::string	fprefix;
	unsigned	i = 1;

	while ((ifs >> fprefix))
		shadow_tags[fprefix] = i++;
}

void ShadowCore::setupInitialState(ExecutionState* es)
{
	SpecialFunctionHandler	*sfh;
	Constant		*f_enter, *f_leave;
	FunctionType		*f_enter_ty, *f_leave_ty;
	KModule			*km;
	Module			*m;
	std::vector<Type *>	argTypes;

	km = exe->getKModule();
	m = km->module;
	sfh = exe->getSFH();

	f_leave_ty = FunctionType::get(
		Type::getVoidTy(getGlobalContext()),
		argTypes,
		false);
	f_leave = m->getOrInsertFunction("shadow_taint_off", f_leave_ty); 

	argTypes.push_back(IntegerType::get(getGlobalContext(), 32));
	f_enter_ty = FunctionType::get(
		Type::getVoidTy(getGlobalContext()),
		argTypes,
		false);
	f_enter = m->getOrInsertFunction("shadow_taint_on", f_enter_ty);

	/* add instrumentation functions */
	sfh->addHandler(shadow_hi[0]);
	sfh->addHandler(shadow_hi[1]);

	/* add function pass to instrument interesting funcs */
	km->addFunctionPass(new ShadowPass(*exe, f_enter, f_leave, shadow_tags));
}

SFH_DEF_HANDLER(TaintOn)
{
}

SFH_DEF_HANDLER(TaintOff)
{
}

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

	bool runOnBasicBlock(llvm::BasicBlock& bi, uint64_t tag);
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

/* instrument all loads to taint data */
bool ShadowPass::runOnBasicBlock(
	llvm::BasicBlock& bi,
	uint64_t tag)
{
	Instruction	*last_load = NULL;
	unsigned	tainted_ins_c = 0;

	foreach (iit, bi.begin(), bi.end()) {
		Instruction		*ii = iit;

		if (last_load != NULL) {
			CallInst *new_call;
			std::vector<Value*>	args;

			args.push_back(
				ConstantInt::get(
					IntegerType::get(getGlobalContext(), 64),
					tag));
			new_call = CallInst::Create(f_enter, args, "", ii);
			last_load = NULL;
			tainted_ins_c++;
		}

		if (ii->getOpcode() == Instruction::Load)
			last_load = ii;
	}

	return (tainted_ins_c != 0);
}
bool ShadowPass::runOnFunction(llvm::Function& f)
{
	std::string			f_name_raw(exe.getPrettyName(&f));
	std::string			f_name;
	shadow_tags_ty::const_iterator	it;
	unsigned			i;
	bool				was_changed = false;

	for (i = 0; f_name_raw[i] && f_name_raw[i] != '+'; i++);
	f_name = f_name_raw.substr(0, i);
	
	it = tags.find(f_name);
	if (it == tags.end())
		return false;

	foreach (bit, f.begin(), f.end()) {
		if (runOnBasicBlock(*bit, it->second))
			was_changed = true;
	}

	return was_changed;
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
	const klee::ConstantExpr	*ce;
	ShadowAlloc			*sa;
	KInstIterator			kii = state.prevPC;
	ref<Expr>			old_expr, tainted_expr;

	--kii;
	old_expr = state.readLocal(kii);
	ce = cast<const klee::ConstantExpr>(arguments[0]);
	sa = static_cast<ShadowAlloc*>(Expr::getAllocator());
	sa->startShadow(ce->getZExtValue());
	tainted_expr = old_expr->rebuild();
	state.bindLocal(kii, tainted_expr);
	sa->stopShadow();
}

SFH_DEF_HANDLER(TaintOff) {}

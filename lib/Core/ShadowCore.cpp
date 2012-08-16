#include <fstream>
#include <iostream>
#include <llvm/Support/CommandLine.h>
#include <llvm/Module.h>
#include <llvm/LLVMContext.h>
#include <llvm/Type.h>
#include "static/Sugar.h"

#include "ShadowObjectState.h"
#include "../Expr/ShadowAlloc.h"
#include "../Expr/ShadowBuilder.h"

#include <assert.h>
#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KModule.h"
#include "SpecialFunctionHandler.h"
#include "Executor.h"
#include "ShadowCore.h"
#include "ShadowPass.h"

#include "MMU.h"

namespace llvm
{
cl::opt<std::string> ShadowFuncFile("shadow-func-file", cl::init("shadow.txt"));
}

using namespace klee;
using namespace llvm;

Executor* ShadowCore::g_exe = NULL;

SFH_HANDLER2(TaintLoad, ShadowCombine* sc)
SFH_HANDLER2(TaintStore, ShadowCombine* sc)

#define TABENT(x,y) {		\
	x,			\
	&Handler##y::create,	\
	false, /* terminates */	\
	false, /* has return value */	\
	false /* do not override intrinsic */ }

static struct SpecialFunctionHandler::HandlerInfo shadow_hi[] =
{
	TABENT("shadow_taint_load", TaintLoad),
	TABENT("shadow_taint_store", TaintStore),
};
#undef TABENT

ShadowCore::ShadowCore(Executor* _exe)
: exe(_exe)
{
	ExprAlloc	*ea;

	loadShadowTags(ShadowFuncFile);
	assert (shadow_tags.size() != 0);

	ea = Expr::getAllocator();
	Expr::setAllocator(new ShadowAlloc());
	delete ea;

	sc = new ShadowCombineOr();
	Expr::setBuilder(ShadowBuilder::create(Expr::getBuilder(), sc));

	delete ObjectState::getAlloc();
	ObjectState::setAlloc(new ObjectStateFactory<ShadowObjectState>());

	g_exe = exe;
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
	Constant		*f_store, *f_load;
	FunctionType		*f_ty;
	HandlerTaintLoad	*t_load_h;
	HandlerTaintStore	*t_store_h;
	KModule			*km;
	Module			*m;
	std::vector<Type *>	argTypes;

	km = exe->getKModule();
	m = km->module;
	sfh = exe->getSFH();

	argTypes.push_back(IntegerType::get(getGlobalContext(), 32));
	f_ty = FunctionType::get(
		Type::getVoidTy(getGlobalContext()),
		argTypes,
		false);
	f_load = m->getOrInsertFunction("shadow_taint_load", f_ty);

	argTypes.clear();
	argTypes.push_back(IntegerType::get(getGlobalContext(), 64));
	argTypes.push_back(IntegerType::get(getGlobalContext(), 64));
	argTypes.push_back(IntegerType::get(getGlobalContext(), 32));
	f_ty = FunctionType::get(
		Type::getVoidTy(getGlobalContext()),
		argTypes,
		false);
	f_store = m->getOrInsertFunction("shadow_taint_store", f_ty);

	/* add instrumentation functions */
	t_load_h = static_cast<HandlerTaintLoad*>(
		sfh->addHandler(shadow_hi[0]));
	t_store_h = static_cast<HandlerTaintStore*>(
		sfh->addHandler(shadow_hi[1]));

	t_load_h->sc = sc;
	t_store_h->sc = sc;

	/* add function pass to instrument interesting funcs */
	km->addFunctionPass(new ShadowPass(*exe, f_load, f_store, shadow_tags));
}

SFH_DEF_HANDLER(TaintLoad)
{
	ShadowAlloc			*sa;
	uint64_t			shadow_tag;
	KInstIterator			kii = state.prevPC;
	ShadowRef			old_shadow;
	ref<Expr>			old_expr, tainted_expr;

	--kii;
	old_expr = state.readLocal(kii);
	shadow_tag = cast<klee::ConstantExpr>(arguments[0])->getZExtValue();

	sa = ShadowAlloc::get();
	old_shadow = ShadowAlloc::getExpr(old_expr);
	if (!old_shadow.isNull()) {
		uint64_t	old_tag;

		old_tag = old_shadow->getShadow();
		if (old_tag == shadow_tag)
			return;

		shadow_tag = sc->combine(shadow_tag, old_tag);
	}

	sa->startShadow(shadow_tag);

	tainted_expr = old_expr->realloc();

	assert (ShadowAlloc::getExpr(tainted_expr).get());

#if 0
	std::cerr << "TAINTED LOAD: " << tainted_expr << '\n';
	std::cerr << "TAINTED INST: ";
	kii->getInst()->dump();
#endif

	state.bindLocal(kii, tainted_expr);
	sa->stopShadow();
}

SFH_DEF_HANDLER(TaintStore)
{
	MMU		*mmu = ShadowCore::getExe()->getMMU();
	ShadowAlloc	*sa;
	ref<Expr>	base(arguments[1]);
	ref<Expr>	value(arguments[0]);
	ShadowRef	old_shadow(ShadowAlloc::getExpr(value));
	uint64_t	shadow_tag;

	shadow_tag = cast<klee::ConstantExpr>(arguments[2])->getZExtValue();

	/* already tainted? */
	if (!old_shadow.isNull() && old_shadow->getShadow() == shadow_tag) {
		MMU::MemOp	mop(true, base, value, 0);
		mmu->exeMemOp(state, mop);
		return;
	}

	if (	old_shadow.isNull() == false &&
		old_shadow->getShadow() != shadow_tag)
	{
		shadow_tag = sc->combine(shadow_tag, old_shadow->getShadow());
	}

	sa = ShadowAlloc::get();
	sa->startShadow(shadow_tag);

	value = value->realloc();

	MMU::MemOp	mop(true, base, value, 0);
	mmu->exeMemOp(state, mop);

	sa->stopShadow();
}

void ShadowCore::addConstraint(ExecutionState &state, ref<Expr>& condition)
{
	ShadowRef	se(ShadowAlloc::getExpr(condition));

	if (se.isNull()) return;
	if (state.prevPC->getForkCount()) return;

	std::cerr << "===============================\n";
	std::cerr	<< "SHADOW COND: " << condition
			<< ". V=" << se->getShadow() << '\n';
	std::cerr << "PREVPC INST: ";
	state.prevPC->getInst()->dump();
	std::cerr << '\n';
	exe->printStackTrace(state, std::cerr);
	std::cerr << "===============================\n";
}

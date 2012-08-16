#include <fstream>
#include <iostream>

#include <llvm/Pass.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Module.h>
#include <llvm/LLVMContext.h>
#include <llvm/Type.h>

#include "Executor.h"
#include "PTree.h"
#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KModule.h"
#include "SpecialFunctionHandler.h"
#include "ShadowObjectState.h"
#include "../Expr/ShadowAlloc.h"
#include "../Expr/ShadowBuilder.h"
#include "ExeStateManager.h"
#include "../Searcher/DFSSearcher.h"
#include "TaintMergeCore.h"

using namespace klee;
using namespace llvm;

#define MERGE_FUNCNAME	"shadow_merge_on_return"

SFH_HANDLER2(TaintMerge, TaintMergeCore* tm)

namespace llvm
{
cl::opt<std::string> TMergeFuncFile("tmerge-func-file", cl::init("tmerge.txt"));
}

class EntryPass : public llvm::FunctionPass
{
private:
	static char		ID;
	Executor		&exe;
	const tm_tags_ty	&tags;
	llvm::Constant		*f_enter;

	bool isShadowedFunc(const llvm::Function& f) const;
public:
	EntryPass(
		Executor& _exe,
		llvm::Constant *_f_enter,
		const tm_tags_ty& st)
	: llvm::FunctionPass(ID)
	, exe(_exe), tags(st), f_enter(_f_enter) {}

	virtual ~EntryPass() {}
	virtual bool runOnFunction(llvm::Function& f);
};
char EntryPass::ID;


bool EntryPass::isShadowedFunc(const llvm::Function& f) const
{
	std::string			f_name_raw(exe.getPrettyName(&f));
	std::string			f_name;
	tm_tags_ty::const_iterator	it;
	unsigned			i;
	
	for (i = 0; f_name_raw[i] && f_name_raw[i] != '+'; i++);
	f_name = f_name_raw.substr(0, i);

	return (tags.count(f_name) > 0);
}

bool EntryPass::runOnFunction(llvm::Function& f)
{
	if (!isShadowedFunc(f))
		return false;

	CallInst::Create(f_enter, MERGE_FUNCNAME, f.begin()->begin());
	return true;
}

static struct SpecialFunctionHandler::HandlerInfo tm_hi =
{ MERGE_FUNCNAME, &HandlerTaintMerge::create, false, false, false };

TaintMergeCore::TaintMergeCore(Executor* _exe)
: exe(_exe)
, taint_id(1)
, merging_st(NULL)
, old_pt(NULL)
, old_esm(NULL)
{
	ExprAlloc	*ea;
	ShadowCombine	*sc;

	loadTags(TMergeFuncFile);
	assert (tm_tags.size() != 0 && "No tagged functions?");

	ea = Expr::getAllocator();
	Expr::setAllocator(new ShadowAlloc());
	delete ea;

	sc = new ShadowCombineOr();
	Expr::setBuilder(ShadowBuilder::create(Expr::getBuilder(), sc));

	delete ObjectState::getAlloc();
	ObjectState::setAlloc(new ObjectStateFactory<ShadowObjectState>());
}

void TaintMergeCore::loadTags(const std::string& s)
{
	std::ifstream	ifs(s.c_str());
	std::string	fprefix;
	unsigned	i = 1;

	while ((ifs >> fprefix)) tm_tags[fprefix] = i++;
}

void TaintMergeCore::setupInitialState(ExecutionState* es)
{
	FunctionType		*f_ty;
	Constant		*f_enter;
	Module			*m;
	KModule			*km;
	HandlerTaintMerge	*t_merge_h;

	km = exe->getKModule();
	m = km->module;
	f_ty = FunctionType::get(Type::getVoidTy(getGlobalContext()), false);
	f_enter = m->getOrInsertFunction(MERGE_FUNCNAME, f_ty);

	t_merge_h = static_cast<HandlerTaintMerge*>(
		exe->getSFH()->addHandler(tm_hi));
	t_merge_h->tm = this;
	km->addFunctionPass(new EntryPass(*exe, f_enter, tm_tags));

}

SFH_DEF_HANDLER(TaintMerge)
{
	if (state.isConcrete()) {
		std::cerr << "[TaintMerge] Ignoring concrete state\n";
		return;
	}

	tm->taintMergeBegin(state);
}

void TaintMergeCore::taintMergeBegin(ExecutionState& state)
{
	ExeStateManager	*new_esm;
	PTree		*new_pt;
	ExecutionState	*new_st;

	if (old_pt != NULL) {
		std::cerr << "[TaintMerge] Ignorign nested merge.\n";
		return;
	}


	/* NOW MERGINGGGGGGGG */
	std::cerr << "BEGIN THE MERGE!!\n";

	/* swap out state information to support exhaustive search */
	old_pt = exe->getPTree();
	old_esm = exe->getStateManager();

	/* activate expression tainting */
	merging_st = &state;
	new_st = merging_st->copy();
	new_pt = new PTree(new_st);

	new_esm = new ExeStateManager();
	new_esm->setInitialState(exe, new_st, false);
	new_esm->setupSearcher(new DFSSearcher());

	exe->setPTree(new_pt);
	exe->setStateManager(new_esm);

	/* at the end, we expect to have a set of states which are all tainted 
	 * these states need to be merged */

	assert (0 == 1 && "MERGE POINT");
}


void TaintMergeCore::taintMergeEnd(void)
{
	assert (merging_st != NULL);

	assert (0 == 1 && "STUB");
#if 0
	while (1) step();
	foreach (new_esm, ..., ...) {
	}
	new_esm->commitQueue
	new_esm->teardownUserSearcher();
	delete new_pt;
	delete new_esm;


#endif
}

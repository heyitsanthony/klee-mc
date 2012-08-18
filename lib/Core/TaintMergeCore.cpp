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
#include "static/Sugar.h"

using namespace klee;
using namespace llvm;

#define MERGE_FUNCNAME		"shadow_merge_on_return"
#define GET_STACK_DEPTH(x)	x->getStackDepth()

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
, old_esm(NULL)
, merge_depth(0)
, merging(false)
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
	ExecutionState	*new_st;

	if (old_esm != NULL) {
		std::cerr << "[TaintMerge] Ignorign nested merge.\n";
		return;
	}

	/* NOW MERGINGGGGGGGG */
	std::cerr << "BEGIN THE MERGE!!\n";
	ShadowAlloc::get()->startShadow(++taint_id);

	/* swap out state information to support exhaustive search */
	old_esm = exe->getStateManager();

	/* activate expression tainting */
	merging_st = &state;
	new_esm = new ExeStateManager();

	new_st = merging_st->copy();

	std::cerr << "COPIED?? STKSZ=" << new_st->getStackDepth() << '\n';
	new_esm->setInitialState(new_st);
	new_esm->setupSearcher(new DFSSearcher());

	std::cerr << "RIGGING NEW STATE MANAGER!!\n";
	exe->setStateManager(new_esm);

	/* run states until their stack is < the current stack,
	 * indicating completion */
	merging = true;
	merge_depth = GET_STACK_DEPTH(merging_st);

	assert (ShadowAlloc::get()->isShadowing());
}

void TaintMergeCore::step(void)
{
	ExeStateManager	*esm;
	ExecutionState	*cur_es;
	static unsigned k = 0;

	if (merging == false) return;

	assert (ShadowAlloc::get()->isShadowing());

	esm = exe->getStateManager();
	cur_es = exe->getCurrentState();
	assert (cur_es->getStackDepth() != 0);
	k++;
	std::cerr << cur_es->getStackDepth() << " vs " << merge_depth << '\n';
	if (k > 10) {
		exe->printStackTrace(*cur_es, std::cerr);
		k = 0;
	}
	if (GET_STACK_DEPTH(cur_es) < merge_depth) {
		std::cerr << "YIELD IT: cur_es->stk = "
			<< GET_STACK_DEPTH(cur_es) << 
			" vs merge=" << merge_depth << '\n';
		std::cerr << "TRACE:\n";
		cur_es->dumpStack(std::cerr);
		std::cerr << "========================\n";
		esm->forceYield(cur_es);
		esm->commitQueue(NULL);
	}

	if (esm->numRunningStates() == 0)
		taintMergeEnd();
}


void TaintMergeCore::taintMergeEnd(void)
{
	/* we expect to have a set of states which are all tainted;
	 * the states should all be yielded in the statemanager */
	ExeStateManager	*esm = exe->getStateManager();
	uint64_t	total_insts = 0;

	assert (merging_st != NULL);

	ShadowAlloc::get()->stopShadow();
	merging = false;

	std::cerr << "YIELDED STATES: " << esm->numYieldedStates() << '\n';
	/* no branches, just play the merging state as normal */
	if (esm->numYieldedStates() == 1) {
		/* FIXME: ideally, we'd want to use the data from
		 * *this* state instead of from the merging_st since
		 * it is what merging_st will eventually equal. However,
		 * the scheduler interaction is dicey, so I don't know
		 * how to do it. */
		/* Caches will hopefully eat up the expensive parts */
		std::cerr << "HOOOOOOOOO-HUM\n";
		goto done;
	}

	foreach (it, esm->beginYielded(), esm->endYielded()) {
		ExecutionState	*es(*it);

		std::cerr << "===================HEY!================\n";

		foreach (it2, es->addressSpace.begin(), es->addressSpace.end()) {
			const ObjectState	*os;
			const ShadowObjectState	*sos;

			os = it2->second;
			sos = dynamic_cast<const ShadowObjectState*>(os);
			if (sos == NULL) continue;
			if (sos->isClean()) continue;
			
			std::cerr << "HEY ADDR:"
				<< (void*)it2->first->address << "--"
				<< (void*)(it2->first->address+sos->size)
				<< '\n';
			std::cerr << "TAINTED: {";
			for (unsigned i = 0; i < sos->size; i++)
				if (sos->isByteTainted(i))
					std::cerr << i << " ";
			std::cerr << "}\n";
		}

		total_insts += es->personalInsts;
	}

	std::cerr << "TOTAL-INST: " << total_insts << '\n';

	/* these states need to be merged */
	assert (0 == 1 && "STUB");
#if 0
	foreach (new_esm, ..., ...) {
	}
	new_esm->commitQueue
	
#endif
done:
	std::cerr << "DELETING DOWN: " << esm << '\n';
	delete esm;
	exe->setStateManager(old_esm);
	old_esm = NULL;
	merging_st = NULL;
}

#include <fstream>
#include <iostream>

#include <llvm/Pass.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>

#include "../Core/Forks.h"
#include "TaintGroup.h"
#include "../Core/Executor.h"
#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KModule.h"
#include "../Core/SpecialFunctionHandler.h"
#include "ShadowObjectState.h"
#include "../Expr/ShadowAlloc.h"
#include "../Expr/ShadowBuilder.h"
#include "../Core/ExeStateManager.h"
#include "../Searcher/DFSSearcher.h"
#include "../Searcher/SearchUpdater.h"
#include "TaintMergeCore.h"
#include "static/Sugar.h"

using namespace klee;
using namespace llvm;

#define MERGE_FUNCNAME		"shadow_merge_on_return"
#define GET_STACK_DEPTH(x)	x->getStackDepth()

SFH_HANDLER2(TaintMerge, TaintMergeCore* tm)


unsigned TaintMergeCore::nested_merge_c = 0;
unsigned TaintMergeCore::merges_c = 0;
unsigned TaintMergeCore::merge_states_c = 0;

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
	std::string			f_name_raw;
	std::string			f_name;
	tm_tags_ty::const_iterator	it;
	unsigned			i;

	f_name_raw = exe.getKModule()->getPrettyName(&f);
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

namespace klee
{
class TaintUpdateAction : public UpdateAction
{
public:
	TaintUpdateAction(const ConstraintManager& cm)
	: baseConstraints(cm) {}
	virtual ~TaintUpdateAction() { }
	virtual UpdateAction* copy(void) const
	{ return new TaintUpdateAction(baseConstraints);  }

	virtual void selectUpdate(ExecutionState* es)
	{
		computeTaint(es);
		ShadowAlloc::get()->startShadow(
			ShadowValExpr::create(lastTaint));
	}

	void computeTaint(ExecutionState* es)
	{
		/* recompute taint value */
		ref<Expr>	lt;
		ConstraintManager	cm(es->constraints - baseConstraints);
		SAVE_SHADOW
		_sa->stopShadow();
		if (cm.size() == 0) lt = MK_CONST(1, 1);
		else lt = BinaryExpr::Fold(Expr::And, cm.begin(), cm.end());
		POP_SHADOW
		lastTaint = ShadowAlloc::drop(lt);
	}
private:
	ConstraintManager	baseConstraints;
	ref<Expr>		lastTaint;
};
}

static struct SpecialFunctionHandler::HandlerInfo tm_hi =
{ MERGE_FUNCNAME, &HandlerTaintMerge::create, false, false, false };

TaintMergeCore::TaintMergeCore(Executor* _exe)
: exe(_exe)
, cur_taint_id(1)
, merging_st(NULL)
, old_esm(NULL)
, merge_depth(0)
, merging(false)
{
	ExprAlloc	*ea;
	ShadowMix	*sc;

	loadTags(TMergeFuncFile);
	assert (tm_tags.size() != 0 && "No tagged functions?");

	ea = Expr::getAllocator();
	Expr::setAllocator(new ShadowAlloc());
	delete ea;

	sc = new ShadowMixAnd();
	Expr::setBuilder(ShadowBuilder::create(Expr::getBuilder(), sc));

	delete ObjectState::getAlloc();
	ObjectState::setAlloc(new ObjectStateFactory<ShadowObjectState>());
}

TaintMergeCore::~TaintMergeCore()
{
	foreach (it, taint_grps.begin(), taint_grps.end())
		delete it->second;
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
	m = km->module.get();
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
		std::cerr << "[TaintMerge] Ignoring nested merge.\n";
		nested_merge_c++;
		return;
	}

	/* NOW MERGINGGGGGGGG */
	std::cerr << "BEGIN THE MERGE ON STATE:\n";
	exe->printStackTrace(state, std::cerr);

	ShadowAlloc::get()->startShadow(ShadowValExpr::create(MK_CONST(1, 1)));

	std::cerr << "WOOOO: EXPRS= " << Expr::getNumExprs() << '\n';

	/* swap out state information to support exhaustive search */
	old_esm = exe->getStateManager();

	/* activate expression tainting */
	merging_st = &state;
	new_esm = new ExeStateManager();

	new_st = merging_st->copy();

	std::cerr << "COPIED?? STKSZ=" << new_st->getStackDepth() << '\n';
	new_esm->setInitialState(new_st);
	tua = new TaintUpdateAction(merging_st->constraints);
	new_esm->setupSearcher(new SearchUpdater(new DFSSearcher(), tua));

	std::cerr << "RIGGING NEW STATE MANAGER!!\n";
	exe->setStateManager(new_esm);

	/* run states until their stack is < the current stack,
	 * indicating completion */
	merging = true;
	merge_depth = GET_STACK_DEPTH(merging_st);
	was_quench = exe->getForking()->isQuenching();

	assert (ShadowAlloc::get()->isShadowing());
}

void TaintMergeCore::step(void)
{
	ExeStateManager	*esm;
	ExecutionState	*cur_es;

	if (merging == false) return;

	assert (ShadowAlloc::get()->isShadowing());

	esm = exe->getStateManager();
	cur_es = exe->getCurrentState();
	assert (cur_es->getStackDepth() != 0);

	if (GET_STACK_DEPTH(cur_es) < merge_depth) {
		std::cerr << "Force Yielding\n";
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
	TaintGroup	*tg;
	bool		forked;

	assert (esm->numRunningStates() == 0);
	assert (merging_st != NULL);

	ShadowAlloc::get()->stopShadow();
	merging = false;

	/* no branches; no control dependencies, handled by taintgroup */
	forked = (esm->numYieldedStates() > 1);

	if (!forked) std::cerr << "[TaintMergeCore] No forks!\n";

	/* merge taint information */
	std::cerr << "[TaintMergeCore] Caught "
		<< esm->numYieldedStates()
		<< " states.\n";
	tg = new TaintGroup();

	if (forked) taint_grps[cur_taint_id] = tg;
	foreach (it, esm->beginYielded(), esm->endYielded())
		tg->addState(*it);

	std::cerr << "TOTAL-INST: " << tg->getInsts() << '\n';

	std::cerr << "INHERITED CONTROL\n";
	merging_st->inheritControl(*(*(esm->beginYielded())));
	merging_st->totalInsts += tg->getInsts();
	std::cerr << "RET FROM MERGE\n";

	/* finally, restore tainted state */
	tg->apply(merging_st);

#if 0
	std::cerr << "TEST0\n";
	foreach (it,
		merging_st->addressSpace.begin(), 
		merging_st->addressSpace.end())
	{
		const ObjectState	*os(it->second);
		for (unsigned i = 0; i < os->size; i++) {
			if (os->read8(i)->isShadowed() == false)
				continue;
			std::cerr << "MO-BASE=" << (void*)it->first->address << '\n';
			std::cerr << "ADDR=" 
				<< (void*)(it->first->address+i) << '\n';
			std::cerr << "[i] = " << i << '\n';
			std::cerr << "OOPS.\n";
			os->print();
			assert(0 == 1);
		}
	}
#endif

	{
	StackFrame	&sf(merging_st->stack[merging_st->stack.size()-1]);
	for (unsigned i = 0; i < sf.kf->numRegisters; i++) {
		ref<Expr>	e(0);
		if (sf.locals[i].value.isNull()) continue;
		e = ShadowAlloc::drop(sf.locals[i].value);
		sf.locals[i].value = e;
		assert (sf.locals[i].value->isShadowed() == false);
	}
	}

#if 0
	foreach (it,
		merging_st->addressSpace.begin(), 
		merging_st->addressSpace.end())
	{
		const ObjectState	*os(it->second);
		for (unsigned i = 0; i < os->size; i++) {
			if (os->read8(i)->isShadowed() == false)
				continue;
			std::cerr << "MO-BASE=" << (void*)it->first->address << '\n';
			std::cerr << "ADDR=" 
				<< (void*)(it->first->address+i) << '\n';
			std::cerr << "[i] = " << i << '\n';
			std::cerr << "OOPS.\n";
			os->print();
			assert(0 == 1);
		}
	}
#endif

	if (!forked) {
		delete tg;
	} else {
		merges_c++;
		merge_states_c += esm->numYieldedStates();
	}

	delete esm;
	exe->setStateManager(old_esm);
	old_esm = NULL;
	tua = NULL;

	merging_st = NULL;
	exe->getForking()->setQuenching(was_quench);
}


void TaintMergeCore::addConstraint(ExecutionState &state, ref<Expr> condition)
{
	if (&state == exe->getCurrentState())
		tua->selectUpdate(&state);
}

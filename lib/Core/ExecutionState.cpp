//===-- ExecutionState.cpp ------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/util/Assignment.h"
#include "klee/ExecutionState.h"
#include "klee/ExeStateBuilder.h"
#include "klee/Solver.h"
#include "../Solver/SMTPrinter.h"
#include "../Expr/ExprReplaceVisitor.h"
#include "Terminator.h"
#include "MemoryManager.h"
#include "ImpliedValue.h"

#include "klee/util/ExprPPrinter.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/ADT/KTest.h"

#include "klee/Expr.h"
#include "static/Sugar.h"

#include "PTree.h"
#include "Memory.h"

#include <llvm/IR/Function.h>
#include <llvm/Support/CommandLine.h>

#include <iostream>
#include <sstream>
#include <iomanip>
#include <cassert>
#include <map>
#include <set>
#include <stdarg.h>
#include <string.h>

using namespace llvm;
using namespace klee;

ExeStateBuilder* ExeStateBuilder::theESB = NULL;
MemoryManager* ExecutionState::mm = NULL;

/* XXX: deactivated */
#include "static/Markov.h"
Markov<KFunction> theStackXfer;

#define LOG_DIR		"statelog/log."
namespace {
	cl::opt<bool>
	LogConstraints(
		"log-constraints",
		cl::desc("Log constraints into SMT as they are added to state."));

	cl::opt<bool>
	TrackStateFuncMinInst(
		"track-state-mininst-kf",
		cl::desc("Track minimum instruction state finds kfunc"));
}

static uint64_t sid_c = 0;

void ExecutionState::initFields(void)
{
	depth = 0;
	weight = 1;
	queryCost = 0.;
	lastNewInst = 0;
	lastGlobalInstCount = 0;
	totalInsts = 0;
	concretizeCount = 0;
	personalInsts = 0;
	newInsts = 0;
	coveredNew = false;
	isReplay = false;
	forkDisabled = false;
	ptreeNode = 0;
	num_allocs = 0;
	prev_constraint_hash = 0;
	isCompactForm = false;
	onFreshBranch = false;
	is_shadowing = false;
	canary = ES_CANARY_VALUE;
	partseed_ktest = NULL;
	partseed_assignment = NULL;
	isEnableMMU = true;
}

/** XXX XXX XXX REFACTOR PLEASEEE **/
ExecutionState::ExecutionState(KFunction *kf)
: pc(kf->instructions)
, prevPC(pc)
, sid(++sid_c)
{
	initFields();
	pushFrame(0, kf);
	replayBrIter = brChoiceSeq.end();
}

ExecutionState::ExecutionState(const std::vector<ref<Expr> > &assumptions)
: constraints(assumptions)
{
	initFields();
	replayBrIter = brChoiceSeq.end();
}

ExecutionState::ExecutionState(void)
{
	initFields();
	replayBrIter = brChoiceSeq.begin();
}

ExecutionState::~ExecutionState()
{
	while (!stack.empty()) popFrame();
	canary = 0;
	if (partseed_assignment) delete partseed_assignment;
}

ExecutionState* ExecutionState::copy(const ExecutionState* es) const
{ return new ExecutionState(*es); }

ExecutionState *ExecutionState::branch(bool forReplay)
{
	ExecutionState *newState;

	assert (forReplay == false && "can't replay right");

	depth++;
	weight *= .5;

	newState = copy();
	newState->sid = ++sid_c;
	newState->isReplay = false;
	newState->coveredNew = false;
	newState->coveredLines.clear();
	
	if (isReplay) {
		newState->brChoiceSeq.truncatePast(newState->replayBrIter);
		/* I'm not happy with adjusting the old state
		 * iterator on a truncation, but this is the only
		 * place it's done, so it's not a problem yet. */
		replayBrIter.reseat();
	}
	newState->replayBrIter = newState->branchesEnd();

	newState->personalInsts = 0;
	newState->newInsts = 0;
	newState->lastNewInst = 0;
	newState->onFreshBranch = false;

	newState->partseed_ktest = NULL;
	newState->partseed_assignment = NULL;

	if (term.get()) {
		assert (newState->term.get() != term.get());
	}

	if (forReplay) newState->compact();

	return newState;
}

void ExecutionState::compact(void)
{
	addressSpace.clear();
	concrete_constraints = ConstraintManager();
	constraints = ConstraintManager();
	stack.clear();
	mallocIterations.clear();
	memObjects.clear();
	symbolics.clear();
	arr2sym.clear();

	isCompactForm = true;
	onFreshBranch = false;
	personalInsts = 0;
	newInsts = 0;

	term = ProtoPtr<Terminator>();
}

void ExecutionState::pushFrame(KInstIterator caller, KFunction *kf)
{
	assert (kf != NULL && "Bad function pushed on stack");

	if (stack.empty() == false) {
		// theStackXfer.insert(getCurrentKFunc(), kf);
		getCurrentKFunc()->addExit(kf);
	}

	kf->incEnters();
	stack.push_back(StackFrame(caller,kf));

	if (TrackStateFuncMinInst) {
		if (min_kf_inst.find(kf) == min_kf_inst.end())
			min_kf_inst.insert(std::make_pair(kf, totalInsts));
	}
}

void ExecutionState::popFrame()
{
	StackFrame	&sf(stack.back());
	KFunction	*last_kf = getCurrentKFunc();

	if (last_kf) last_kf->incExits();
	foreach (it, sf.allocas.begin(), sf.allocas.end())
		unbindObject(*it);
	stack.pop_back();
#if 0
	if (stack.empty() == false && last_kf) {
		theStackXfer.insert(last_kf, getCurrentKFunc());
		last_kf->addExit(getCurrentKFunc());
	}
#endif
}

void ExecutionState::xferFrame(KFunction* kf)
{
	CallPathNode		*cpn;
	ref<Expr>		retexpr;
	KFunction		*retf;
	KInstIterator		ki = getCaller();

	assert (kf != NULL);
	assert (stack.size() > 0);

	// theStackXfer.insert(getCurrentKFunc(), kf);
	getCurrentKFunc()->addExit(kf);

	StackFrame	&sf(stack.back());

	/* save, pop off old state */
	cpn = sf.callPathNode;
	retf = sf.onRet;
	retexpr = sf.onRet_expr;
	foreach (it, sf.allocas.begin(), sf.allocas.end())
		unbindObject(*it);
	sf.kf->incExits();
	stack.pop_back();

	/* create new frame to replace old frame;
	   new frame initialized with target function kf */
	stack.push_back(StackFrame(ki, kf));
	StackFrame	&sf2(stack.back());

	sf2.callPathNode = cpn;
	sf2.onRet = retf;
	sf2.onRet_expr = retexpr;

	kf->incEnters();

	if (TrackStateFuncMinInst) {
		if (min_kf_inst.find(kf) == min_kf_inst.end())
			min_kf_inst.insert(std::make_pair(kf, totalInsts));
	}
}

void ExecutionState::bindObject(const MemoryObject *mo, ObjectState *os)
{ addressSpace.bindObject(mo, os); }

void ExecutionState::unbindObject(const MemoryObject* mo)
{
	addressSpace.unbindObject(mo);
	/* XXX: toslow? use set? */
	foreach (it, memObjects.begin(), memObjects.end()) {
		if (it->get() == mo) {
			memObjects.erase(it);
			break;
		}
	}
}

void ExecutionState::rebindObject(const MemoryObject* mo, ObjectState* os)
{
	addressSpace.unbindObject(mo);
	addressSpace.bindObject(mo, os);
}

void ExecutionState::write64(
	ObjectState* object, unsigned offset, uint64_t value)
{
	uint64_t	v = value;
	/* XXX: probably not endian friendly */
	for (int i = 0; i < 8; i++) {
		write8(object, offset+i, v & 0xff);
		v >>= 8;
	}
}

bool ExecutionState::addConstraint(ref<Expr> constraint)
{
	bool	ok;

	ok = constraints.addConstraint(constraint);
	if (LogConstraints) {
		Query		q(constraints, ConstantExpr::create(1, 1));

		SMTPrinter::dump(
			q,
			(std::string(LOG_DIR)+
				llvm::utohexstr(prev_constraint_hash)).c_str());
		prev_constraint_hash = q.hash();
	}

	return ok;
}

KInstIterator ExecutionState::getCaller(void) const
{ return stack.back().caller; }

void ExecutionState::copy(
	ObjectState* os, const ObjectState* reallocFrom, unsigned count)
{
	for (unsigned i=0; i<count; i++) {
		write(os, i, read8(reallocFrom, i));
	}
}

void ExecutionState::dumpStack(std::ostream& os) const
{
  unsigned idx = 0;
  const KInstruction *target = prevPC;
  foreach (it, stack.rbegin(), stack.rend())
  {
    const StackFrame &sf(*it);
    Function *f = sf.kf->function;
    const InstructionInfo &ii(*target->getInfo());
    os << "\t#" << idx++
        << " " << std::setw(8) << std::setfill('0') << ii.assemblyLine
        << " in " << f->getName().str() << " (";
    // we could go up and print varargs if we wanted to.
    unsigned index = 0;
    foreach (ai, f->arg_begin(), f->arg_end())
    {
      if (ai!=f->arg_begin()) os << ", ";

      os << ai->getName().str();
      // XXX should go through function
      ref<Expr> value;
      value = stack.getLocalCell(
	     stack.size() - idx, sf.kf->getArgRegister(index++)).value;
      if (!value.isNull() && isa<ConstantExpr>(value))
        os << "=" << value;
    }
    os << ")";
    if (ii.file != "")
      os << " at " << ii.file << ":" << ii.line;
    os << "\n";
    target = sf.caller;
  }
}

std::ostream &klee::operator<<(std::ostream &os, const MemoryMap &mm)
{
	MemoryMap::iterator it(mm.begin()), ie(mm.end());

	os << "{";
	if (it != ie) {
		os << "MO" << it->first->id << ": (";
		it->first->print(os);
		os << ") " << it->second;
		for (++it; it!=ie; ++it) {
			os << ", \nMO" << it->first->id << ": (";
			it->first->print(os);
			os << ") " << it->second;
		}
	}
	os << "}";
	return os;
}

void ExecutionState::bindArgument(
	KFunction *kf, unsigned index, ref<Expr> value)
{ stack.writeLocalCell(kf->getArgRegister(index), value); }

void ExecutionState::bindLocal(KInstruction* target, ref<Expr> value)
{ stack.writeLocalCell(target->getDest(), value); }

void ExecutionState::transferToBasicBlock(BasicBlock *dst, BasicBlock *src)
{
	// Note that in general phi nodes can reuse phi values from the same
	// block but the incoming value is the eval() result *before* the
	// execution of any phi nodes. this is pathological and doesn't
	// really seem to occur, but just in case we run the PhiCleanerPass
	// which makes sure this cannot happen and so it is safe to just
	// eval things in order. The PhiCleanerPass also makes sure that all
	// incoming blocks have the same order for each PHINode so we only
	// have to compute the index once.
	//
	// With that done we simply set an index in the state so that PHI
	// instructions know which argument to eval, set the pc, and continue.

	// XXX this lookup has to go ?
	KFunction	*kf;
	unsigned	entry;

	kf = getCurrentKFunc();
	entry = kf->getBasicBlockEntry(dst);
	pc = &kf->instructions[entry];

	if (pc->getInst()->getOpcode() == Instruction::PHI) {
		PHINode *phi = static_cast<PHINode*>(pc->getInst());
		incomingBBIndex = phi->getBasicBlockIndex(src);
	}
}

const ObjectState* ExecutionState::bindMemObj(
	const MemoryObject *mo,
	const Array *array)
{
	ObjectState *os;
	if (array)
		os = ObjectState::create(mo->size, ARR2REF(array));
	else
		os = ObjectState::createDemandObj(mo->size);
	bindObject(mo, os);
	return os;
}

ObjectState* ExecutionState::bindMemObjWriteable(
	const MemoryObject *mo, const Array *array)
{
	const ObjectState	*os_c;

	os_c = bindMemObj(mo, array);
	if (os_c == NULL)
		return NULL;

	return addressSpace.getWriteable(mo, os_c);
}


const ObjectState* ExecutionState::bindStackMemObj(
	const MemoryObject *mo,
	const Array *array)
{
	const ObjectState* os;

	os = bindMemObj(mo, array);

	// It's possible that multiple bindings of the same mo in the state
	// will put multiple copies on this list, but it doesn't really
	// matter because all we use this list for is to unbind the object
	// on function return.
	stack.back().addAlloca(mo);
	return os;
}

ObjectPair ExecutionState::allocate(
	uint64_t size, bool isLocal, bool isGlobal,
	const llvm::Value *allocSite)
{
	MemoryObject		*mo;
	const ObjectState	*os;

	if (size == 0) {
		std::cerr << "[ExeState] Fixing up size=0 allocate\n";
		size = 1;
	}

	mo = mm->allocate(size, isLocal, isGlobal, allocSite, this);
	if (mo == NULL)
		return ObjectPair(NULL, NULL);

	num_allocs++;
	os = (isLocal) ? bindStackMemObj(mo) : bindMemObj(mo);

	return ObjectPair(mo, os);
}

std::vector<ObjectPair> ExecutionState::allocateAlignedChopped(
	uint64_t size, unsigned pow2, const llvm::Value *allocSite)
{
	std::vector<MemoryObject*>	mos;
	std::vector<ObjectPair>		os;

	mos = mm->allocateAlignedChopped(size, pow2, allocSite, this);
	if (mos.size() == 0)
		return os;

	num_allocs++;
	foreach (it, mos.begin(), mos.end()) {
		const ObjectState	*cur_os;

		cur_os = bindMemObj(*it);
		assert (cur_os != NULL);
		os.push_back(ObjectPair(*it, cur_os));
	}

	return os;
}

const ObjectState* ExecutionState::allocateFixed(
	uint64_t address, uint64_t size,
	const llvm::Value *allocSite)
{
	MemoryObject	*mo;

	mo = mm->allocateFixed(address, size, allocSite, this);
	if (mo == NULL)
		return NULL;

	num_allocs++;
	return bindMemObj(mo);
}

const ObjectState* ExecutionState::allocateAt(
	uint64_t address, uint64_t size, const llvm::Value *allocSite)
{
	MemoryObject	*mo;

	mo = mm->allocateAt(*this, address, size, allocSite);
	if (mo == NULL)
		return NULL;

	num_allocs++;
	return bindMemObj(mo);
}

ExecutionState* ExecutionState::copy(void) const { return copy(this); }

/**
 * this is kind of a cheat-- we want to read from an object state
 * but we want the root symbolic access-- not the most recent state.
 */
ref<Expr> ExecutionState::readSymbolic(
	const ObjectState* obj, unsigned offset, Expr::Width w) const
{
	const Array	*root_arr;
	unsigned	NumBytes;
	ref<Expr>	ret;

	assert (w % 8 == 0 && w > 0);

	NumBytes = w / 8;
	root_arr = obj->getArray();
	assert (root_arr != NULL);
	assert (root_arr->isSymbolicArray());

	for (unsigned i = 0; i != NumBytes; i++) {
		unsigned	idx;
		ref<Expr>	Byte;

		idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);
		Byte = Expr::createTempRead(ARR2REF(root_arr), 8, idx+offset);

		ret = i ? ConcatExpr::create(Byte, ret) : Byte;
	}

	return ret;
}

void ExecutionState::printConstraints(std::ostream& os) const
{
	Query	q(constraints, ConstantExpr::create(1, 1));
	SMTPrinter::print(os, q);
}

void ExecutionState::getConstraintLog(std::string &res) const
{
	std::ostringstream info;
	ExprPPrinter::printConstraints(info, constraints);
	res = info.str();
}

bool ExecutionState::setupCallVarArgs(
	unsigned funcArgs, std::vector<ref<Expr> >& args)
{
	ObjectPair	op;
	ObjectState	*os;
	unsigned	size, offset, callingArgs;

	StackFrame &sf = stack.back();

	callingArgs = args.size();
	size = 0;
	for (unsigned i = funcArgs; i < callingArgs; i++) {
	// FIXME: This is really specific to the architecture, not the pointer
	// size. This happens to work fir x86-32 and x86-64, however.
		Expr::Width WordSize = Context::get().getPointerWidth();
		if (WordSize == Expr::Int32) {
			size += Expr::getMinBytesForWidth(args[i]->getWidth());
		} else {
			size += llvm::RoundUpToAlignment(
				args[i]->getWidth(), WordSize)/8;
		}
	}

	op = allocateGlobal(size, prevPC->getInst());
	if (op_mo(op) == NULL)
		return false;

	os = addressSpace.getWriteable(op);
	if (os == NULL)
		return false;

	sf.varargs = op_mo(op);

	offset = 0;
	for (unsigned i = funcArgs; i < callingArgs; i++) {
	// FIXME: This is really specific to the architecture, not the pointer
	// size. This happens to work fir x86-32 and x86-64, however.
		Expr::Width WordSize = Context::get().getPointerWidth();
		if (WordSize == Expr::Int32) {
			write(os, offset, args[i]);
			offset += Expr::getMinBytesForWidth(args[i]->getWidth());
		} else {
			assert (WordSize==Expr::Int64 && "Unknown word size!");

			write(os, offset, args[i]);
			offset += llvm::RoundUpToAlignment(
				args[i]->getWidth(),
				WordSize) / 8;
		}
	}

	return true;
}

void ExecutionState::abortInstruction(void)
{
	--prevPC;
	--pc;
	totalInsts--;
}

bool ExecutionState::isConcrete(void) const
{
	foreach (it, symbolics.begin(), symbolics.end()) {
		if (it->getConcretization() == NULL)
			return false;
	}
	return true;
}

void ExecutionState::assignSymbolics(const Assignment& a)
{
	foreach (it, symbolics.begin(), symbolics.end()) {
		SymbolicArray			&sa = *it;
		const std::vector<uint8_t>	*v;

		// we might see a repeat because of a concretized symbolic
		// array because the assignment passed in is
		// the assignment for all symbolics in the state
		//
		// however, the assignment should match up with the current
		// concretization since it's bound before the query
		// is submitted.
		//
		// This could check for equality, but it would be slow; ignore
		if (sa.getConcretization())
			continue;

		v = a.getBinding(sa.getArray());
		if (v == NULL)
			continue;

		sa.setConcretization(*v);
	}


	foreach (it, constraints.begin(), constraints.end())
		concrete_constraints.addConstraint(*it);

	constraints = ConstraintManager();
}

/* kind of stupid-- probably shouldn't loop like this */
std::string ExecutionState::getArrName(const char* arrPrefix)
{
	unsigned	k = 1;
	unsigned	pre_len;

	pre_len = strlen(arrPrefix);
	foreach (it, symbolics.begin(), symbolics.end()) {
		if (memcmp(arrPrefix, it->getArray()->name.c_str(), pre_len))
			continue;
		k++;
	}

	return arrPrefix + ("_" + llvm::utostr(k));
}

void ExecutionState::setPartSeed(const KTest* kt)
{
	if (kt == NULL) {
		if (partseed_assignment == NULL)
			return;

		partseed_assignment = NULL;
		partseed_idx = 0;
		partseed_ktest = NULL;
		return;
	}

	if (partseed_assignment != NULL)
		delete partseed_assignment;

	partseed_ktest = kt;
	partseed_idx = 0;
	partseed_assignment = new Assignment();
}

void ExecutionState::addSymbolic(MemoryObject* mo, Array* array)
{
#if 0
	/* debugging */
	foreach (it, symbolics.begin(), symbolics.end()) {
		if ((*it).getArray()->name == array->name) {
			std::cerr << "WTFFFFFFFF!!!!!!!!!!!\n";
			array->print(std::cerr);
			assert (0 == 1);
		}
	}
#endif
	symbolics.push_back(SymbolicArray(mo, array));
	arr2sym[array] = mo;

	if (partseed_assignment != NULL)
		updatePartSeed(array);
}

void ExecutionState::updatePartSeed(Array *arr)
{
	const uint8_t		*v_buf;
	unsigned		v_len;

	if (partseed_idx >= partseed_ktest->numObjects) {
		/* partseeds exhausted-- drop everything */
		std::cerr << "[ExeState] Partseed exhausted. Godspeed.\n";
		setPartSeed(NULL);
		return;
	}

	if (arr->getSize() != partseed_ktest->objects[partseed_idx].numBytes) {
		std::cerr << "[ExeState] Partseed mismatch. Bail out\n";
		setPartSeed(NULL);
		return;
	}

	v_buf = partseed_ktest->objects[partseed_idx].bytes;
	v_len = partseed_ktest->objects[partseed_idx].numBytes;

	std::vector<uint8_t>	v(v_buf, v_buf + v_len);
	partseed_assignment->addBinding(arr, v);

	partseed_idx++;
}

void ExecutionState::trackBranch(int condIndex, const KInstruction* ki)
{
	/* do not track if still replaying */
	if (!isReplayDone()) return;

	brChoiceSeq.push_back(condIndex, ki);

	replayBrIter = branchesEnd();
}

ExecutionState* ExecutionState::createReplay(
	ExecutionState& initialState,
	const ReplayPath& replayPath)
{
	ExecutionState* newState;

	std::cerr << "CREATE REPLAYS\n";
	newState = initialState.copy();
	foreach (it, replayPath.begin(), replayPath.end()) {
		newState->brChoiceSeq.push_back(*it);
	}

	newState->replayBrIter = newState->brChoiceSeq.begin();
	if (newState->ptreeNode) newState->ptreeNode->markReplay();
	newState->isReplay = true;
	newState->personalInsts = 0;
	newState->newInsts = 0;

	return newState;
}

/* takes a partial path and slaps a replay on it */
/* XXX: this should use insert() */
void ExecutionState::joinReplay(const ReplayPath &replayPath)
{
	unsigned			head_len, c;
	ReplayPath::const_iterator	rp_it, rp_end;
	BranchTracker::iterator		new_rpBrIt, rpBrIt;

	head_len = replayHeadLength(replayPath);
	assert (isReplayDone());
	assert (head_len != 0 && "No head to join!");

	ptreeNode->markReplay();
	isReplay = true;

	rp_it = replayPath.begin();
	rp_end = replayPath.end();
	for (unsigned i = 0; i < head_len && rp_it != rp_end; i++)
		++rp_it;

	assert (rp_it != rp_end);

	c = 0;
	replayBrIter = branchesEnd();
	while (rp_it != rp_end) {
		ReplayNode	rn(*rp_it);
		brChoiceSeq.push_back(rn);
		rp_it++;
		c++;
	}

	assert (head_len <= replayHeadLength(replayPath));

	// brChoiceSeq.verifyPath(replayPath);

	/* replay back to specific place */
	replayBrIter = branchesBegin();
	for (unsigned i = 0; i < head_len; i++)
		replayBrIter++;

	assert (!isReplayDone());
}

bool ExecutionState::isReplayDone(void) const
{ return (replayBrIter == branchesEnd()); }


unsigned ExecutionState::peekReplay(void) const
{
#ifdef INCLUDE_INSTR_ID_IN_PATH_INFO
//	Replay::checkPC(prevPC, replayBrIter);
	assert (prevPC->getInfo()->assemblyLine == (*replayBrIter).second &&
	      "branch instruction IDs do not match");
#endif
	return (*replayBrIter).first;
}

unsigned ExecutionState::stepReplay(void)
{
	unsigned targetIndex = peekReplay();
	++replayBrIter;
	return targetIndex;
}

unsigned ExecutionState::getBrSeq(void) const
{ return replayBrIter.getSeqIdx(); }

ReplayNode ExecutionState::branchLast(void) const
{
	if (brChoiceSeq.empty())
		return ReplayNode();
	return brChoiceSeq.back();
}

ExecutionState* ExecutionState::reconstitute(
	ExecutionState &initialStateCopy) const
{
	ExecutionState* newState;

	newState = copy(&initialStateCopy);
	newState->brChoiceSeq = brChoiceSeq;
	newState->replayBrIter = newState->brChoiceSeq.begin();
	newState->weight = weight;
	newState->personalInsts = 0;

	return newState;
}

void ExecutionState::commitIVC(
	const ref<ReadExpr>& re, const ref<ConstantExpr>& ce)
{
	const MemoryObject	*mo;
	const ObjectState	*os;
	ObjectState		*wos;
	ConstantExpr		*off = dyn_cast<ConstantExpr>(re->index);

	if (off == NULL) return;

	mo = findMemoryObject(re->updates.getRoot().get());
	if (mo == NULL) return;

	assert (mo != NULL && "Could not find MO?");
	os = addressSpace.findObject(mo);

	// os = 0 => obj has been freed
	if (os != NULL) {
		assert(	!os->readOnly && "read only obj with static read?");

		wos = addressSpace.getWriteable(mo, os);
		assert (wos != NULL && "Could not get writable ObjectState?");

		wos->writeIVC(off->getZExtValue(), ce);
	}

	ImpliedValue::ivcStack(stack, re, ce);
	ImpliedValue::ivcMem(addressSpace, re, ce);
}

void ExecutionState::printFileLine(void)
{
	const InstructionInfo &ii = *pc->getInfo();
	const Function* f;

	if (!ii.file.empty()) {
		std::cerr << "     " << ii.file << ':' << ii.line << ':';
		return;
	}

	f = pc->getFunction();
	if (f != NULL) {
		std::cerr << "     " << f->getName().str() << ':';
		return;
	}

	std::cerr << "     [no debug info]:";
}

unsigned ExecutionState::getStackDepth(void) const
{ return stack.size(); }

void ExecutionState::inheritControl(ExecutionState& es)
{
	stack = es.stack;
	pc = es.pc;
	prevPC = es.prevPC;
}

void ExecutionState::printMinInstKFunc(std::ostream& os) const
{
	foreach (it, min_kf_inst.begin(), min_kf_inst.end()) {
		os	<< ((*it).second) << ","
			<< ((*it).first)->function->getName().str()
			<< '\n';
	}
}

unsigned ExecutionState::replayHeadLength(const ReplayPath& rp) const
{
	unsigned			node_c;
	ReplayPath::const_iterator	rpIt, rpItEnd;

	if (!isReplayDone())
		return 0;

	node_c = 0;

	rpIt = rp.begin();
	rpItEnd = rp.end();

	if (brChoiceSeq.size() > rp.size())
		return 0;

	/* must match *every* branch made by the state */
	foreach (brIt, branchesBegin(), replayBrIter) {
		if ((*rpIt).first != (*brIt).first)
			return 0;

		node_c++;
		rpIt++;
	}

	/* perfect match, hm. */
	return node_c;
}

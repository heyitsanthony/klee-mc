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
#include "MemoryManager.h"
#include "ImpliedValue.h"

#include "klee/util/ExprPPrinter.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"

#include "klee/Expr.h"
#include "static/Sugar.h"

#include "PTree.h"
#include "Memory.h"

#include <llvm/Function.h>
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
		cl::desc("Log constraints into SMT as they are added to state."),
		cl::init(false));
}

/** XXX XXX XXX REFACTOR PLEASEEE **/
ExecutionState::ExecutionState(KFunction *kf)
: depth(0)
, weight(1)
, pc(kf->instructions)
, prevPC(pc)
, queryCost(0.)
, instsSinceCovNew(0)
, lastGlobalInstCount(0)
, totalInsts(0)
, concretizeCount(0)
, personalInsts(0)
, coveredNew(false)
, isReplay(false)
, forkDisabled(false)
, ptreeNode(0)
, num_allocs(0)
, prev_constraint_hash(0)
, isCompactForm(false)
, onFreshBranch(false)
, is_shadowing(false)
{
	canary = ES_CANARY_VALUE;
	pushFrame(0, kf);
	replayBrIter = brChoiceSeq.end();
}

ExecutionState::ExecutionState(const std::vector<ref<Expr> > &assumptions)
: constraints(assumptions)
, queryCost(0.)
, lastGlobalInstCount(0)
, totalInsts(0)
, concretizeCount(0)
, personalInsts(0)
, isReplay(false)
, ptreeNode(0)
, num_allocs(0)
, prev_constraint_hash(0)
, isCompactForm(false)
, onFreshBranch(false)
, is_shadowing(false)
{
	canary = ES_CANARY_VALUE;
	replayBrIter = brChoiceSeq.end();
}

ExecutionState::ExecutionState(void)
: lastGlobalInstCount(0)
, totalInsts(0)
, concretizeCount(0)
, coveredNew(false)
, isReplay(false)
, ptreeNode(0)
, num_allocs(0)
, prev_constraint_hash(0)
, isCompactForm(false)
, onFreshBranch(false)
, is_shadowing(false)
{
	canary = ES_CANARY_VALUE;
	replayBrIter = brChoiceSeq.begin();
}

ExecutionState::~ExecutionState()
{
	while (!stack.empty()) popFrame();
	canary = 0;
}

ExecutionState *ExecutionState::branch(bool forReplay)
{
	ExecutionState *newState;

	depth++;
	weight *= .5;

	if (forReplay) {
		newState = compact();
		newState->coveredNew = false;
		return newState;
	}

	newState = copy();
	newState->coveredNew = false;
	newState->coveredLines.clear();
	newState->replayBrIter = newState->brChoiceSeq.end();
	newState->personalInsts = 0;
	newState->onFreshBranch = false;

	return newState;
}

ExecutionState *ExecutionState::compact() const
{
	ExecutionState *newState = ExeStateBuilder::create();
	compact(newState);
	return newState;
}

void ExecutionState::compact(ExecutionState* newState) const
{
	newState->isCompactForm = true;
	newState->brChoiceSeq = brChoiceSeq;
	newState->weight = weight;
	newState->coveredLines.clear();

	// necessary for WeightedRandomSearcher?
	newState->pc = pc;
	newState->onFreshBranch = false;
	newState->personalInsts = 0;
}

void ExecutionState::pushFrame(KInstIterator caller, KFunction *kf)
{
	assert (kf != NULL && "Bad function pushed on stack");

	if (stack.empty() == false) {
		// theStackXfer.insert(getCurrentKFunc(), kf);
		getCurrentKFunc()->addExit(kf);
	}

	stack.push_back(StackFrame(caller,kf));
}

void ExecutionState::popFrame()
{
	StackFrame &sf = stack.back();
	KFunction	*last_kf = getCurrentKFunc();

	foreach (it, sf.allocas.begin(), sf.allocas.end())
		unbindObject(*it);
	stack.pop_back();

	if (stack.empty() == false && last_kf) {
		// theStackXfer.insert(last_kf, getCurrentKFunc());
		// last_kf->addExit(getCurrentKFunc());
	}
}

void ExecutionState::xferFrame(KFunction* kf)
{
	CallPathNode		*cpn;
	KInstIterator		ki = getCaller();

	assert (kf != NULL);
	assert (stack.size() > 0);

	// theStackXfer.insert(getCurrentKFunc(), kf);
	getCurrentKFunc()->addExit(kf);

	/* save, pop off old state */
	cpn = stack.back().callPathNode;

	// pop frame
	StackFrame	&sf(stack.back());
	foreach (it, sf.allocas.begin(), sf.allocas.end())
		unbindObject(*it);
	stack.pop_back();

	/* create new frame to replace old frame;
	   new frame initialized with target function kf */
	stack.push_back(StackFrame(ki,kf));
	StackFrame	&sf2(stack.back());
	sf2.callPathNode = cpn;
}

void ExecutionState::bindObject(const MemoryObject *mo, ObjectState *os)
{
	addressSpace.bindObject(mo, os);
}

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

bool ExecutionState::hasLocal(KInstruction *target) const
{
	if (stack.empty()) return false;

	const StackFrame& sf(stack[stack.size() - 1]);

	if (target->getDest() >= sf.kf->numRegisters) return false;

	return true;
}

Cell& ExecutionState::readLocalCell(unsigned sfi, unsigned i) const
{
	assert(sfi < stack.size());
	const StackFrame& sf = stack[sfi];

	KFunction* kf = sf.kf;
	assert(i < kf->numRegisters);

	return sf.locals[i];
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

Cell& ExecutionState::getLocalCell(unsigned sfi, unsigned i) const
{
	assert(sfi < stack.size());
	const StackFrame& sf = stack[sfi];
	assert(i < sf.kf->numRegisters);
	return sf.locals[i];
}

void ExecutionState::writeLocalCell(unsigned sfi, unsigned i, ref<Expr> value)
{
	assert(sfi < stack.size());
	const StackFrame& sf = stack[sfi];
	KFunction* kf = sf.kf;
	assert(i < kf->numRegisters);

	sf.locals[i].value = value;
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
    const InstructionInfo &ii = *target->getInfo();
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
      value = getLocalCell(
     stack.size() - idx, sf.kf->getArgRegister(index++)).value;
      if (isa<ConstantExpr>(value))
        os << "=" << value;
    }
    os << ")";
    if (ii.file != "")
      os << " at " << ii.file << ":" << ii.line;
    os << "\n";
    target = sf.caller;
  }
}

/**/

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
{ writeLocalCell(stack.size() - 1, kf->getArgRegister(index), value); }

void ExecutionState::bindLocal(KInstruction* target, ref<Expr> value)
{ writeLocalCell(stack.size() - 1, target->getDest(), value); }

ref<Expr> ExecutionState::readLocal(KInstruction* target) const
{ return readLocalCell(stack.size() - 1, target->getDest()).value; }

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
}

void ExecutionState::trackBranch(int condIndex, const KInstruction* ki)
{
	// only track NON-internal branches
	if (replayBrIter != brChoiceSeq.end())
		return;

	brChoiceSeq.push_back(condIndex, ki);
	replayBrIter = brChoiceSeq.end();
}

ExecutionState* ExecutionState::createReplay(
	ExecutionState& initialState,
	const ReplayPathType& replayPath)
{
	ExecutionState* newState;

	newState = initialState.copy();
	foreach (it, replayPath.begin(), replayPath.end()) {
		newState->brChoiceSeq.push_back(*it);
	}

	newState->replayBrIter = newState->brChoiceSeq.begin();
	newState->ptreeNode->markReplay();
	newState->isReplay = true;
	newState->personalInsts = 0;

	return newState;
}

bool ExecutionState::isReplayDone(void) const
{ return (replayBrIter == brChoiceSeq.end()); }

unsigned ExecutionState::stepReplay(void)
{
#ifdef INCLUDE_INSTR_ID_IN_PATH_INFO
    assert (prevPC->getInfo()->assemblyLine == (*replayBrIter).second &&
      "branch instruction IDs do not match");
#endif
    unsigned targetIndex = (*replayBrIter).first;
    ++replayBrIter;
    return targetIndex;
}

BranchInfo ExecutionState::branchLast(void) const
{
	if (brChoiceSeq.empty())
		return BranchInfo();
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

	// os = 0 => obj has been free'd,
	// no need to concretize (although as in other cases we
	// would like to concretize the outstanding
	// reads, but we have no facility for that yet)
	if (os == NULL) return;

	assert(	!os->readOnly && "read only object with static read?");

	wos = addressSpace.getWriteable(mo, os);
	assert (wos != NULL && "Could not get writable ObjectState?");

	wos->writeIVC(off->getZExtValue(), ce);

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

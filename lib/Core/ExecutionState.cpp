//===-- ExecutionState.cpp ------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/ExecutionState.h"

#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"

#include "klee/Expr.h"
#include "static/Sugar.h"

#include "PTree.h"
#include "Memory.h"
#include "OpenfdRegistry.h"

#include "llvm/Function.h"
#include "llvm/Support/CommandLine.h"

#include <iostream>
#include <iomanip>
#include <cassert>
#include <map>
#include <set>
#include <stdarg.h>

using namespace llvm;
using namespace klee;

namespace {
  cl::opt<bool>
  DebugLogStateMerge("debug-log-state-merge");
}

/***/

/** XXX XXX XXX REFACTOR PLEASEEE **/
ExecutionState::ExecutionState(KFunction *kf)
  : fakeState(false),
    underConstrained(false),
    depth(0),
    pc(kf->instructions),
    prevPC(pc),
    queryCost(0.),
    weight(1),
    addressSpace(),
    instsSinceCovNew(0),
    coveredNew(false),
    lastChosen(0),
    isCompactForm(false),
    isReplay(false),
    forkDisabled(false),
    ptreeNode(0)
{
  pushFrame(0, kf);
  replayBranchIterator = branchDecisionsSequence.end();
}

ExecutionState::ExecutionState(const std::vector<ref<Expr> > &assumptions)
  : fakeState(true),
    underConstrained(false),
    constraints(assumptions),
    queryCost(0.),
    addressSpace(),
    lastChosen(0),
    isCompactForm(false),
    isReplay(false),
    ptreeNode(0)
{
  replayBranchIterator = branchDecisionsSequence.end();
}

ExecutionState::~ExecutionState()
{
	OpenfdRegistry::stateDestroyed(this);
	while (!stack.empty()) popFrame();
}

ExecutionState *ExecutionState::branch()
{
	ExecutionState *newState;

	depth++;
	weight *= .5;

	newState = new ExecutionState(*this);
	newState->coveredNew = false;
	newState->coveredLines.clear();
	newState->replayBranchIterator = newState->branchDecisionsSequence.end();

	return newState;
}

void ExecutionState::bindObject(const MemoryObject *mo, ObjectState *os)
{
	addressSpace.bindObject(mo, os);
}

ExecutionState *ExecutionState::branchForReplay(void)
{
	ExecutionState* newState;

	depth++;
	weight *= .5;

	newState = compact();
	newState->coveredNew = false;
	newState->coveredLines.clear();

	return newState;
}

ExecutionState *ExecutionState::compact() const
{
  ExecutionState *newState = new ExecutionState();

  newState->isCompactForm = true;
  newState->branchDecisionsSequence = branchDecisionsSequence;
  newState->weight = weight;

  // necessary for WeightedRandomSearcher?
  newState->pc = pc;

  return newState;
}

ExecutionState* ExecutionState::reconstitute(
	ExecutionState &initialStateCopy) const
{
	ExecutionState* newState;

	newState = new ExecutionState(initialStateCopy);
	newState->branchDecisionsSequence = branchDecisionsSequence;
	newState->replayBranchIterator = newState->branchDecisionsSequence.begin();
	newState->weight = weight;

	return newState;
}

void ExecutionState::pushFrame(KInstIterator caller, KFunction *kf)
{
	assert (kf != NULL);
	stack.push_back(StackFrame(caller,kf));
}

void ExecutionState::popFrame()
{
  StackFrame &sf = stack.back();
  foreach (it, sf.allocas.begin(), sf.allocas.end())
  	unbindObject(*it);
  stack.pop_back();
}

void ExecutionState::unbindObject(const MemoryObject* mo)
{
    addressSpace.unbindObject(mo);
}

void ExecutionState::write64(ObjectState* object, unsigned offset, uint64_t value)
{
	uint64_t	v = value;
	/* XXX: probably not endian friendly */
	for (int i = 0; i < 8; i++) {
		write8(object, offset+i, v & 0xff);
		v >>= 8;
	}
}

std::string ExecutionState::getFnAlias(std::string fn)
{
	std::map < std::string, std::string >::iterator it = fnAliases.find(fn);
	return (it != fnAliases.end()) ? it->second : "";
}

void ExecutionState::addFnAlias(std::string old_fn, std::string new_fn) {
  fnAliases[old_fn] = new_fn;
}

void ExecutionState::removeFnAlias(std::string fn) {
  fnAliases.erase(fn);
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
	return constraints.addConstraint(constraint);
}

Cell& ExecutionState::getLocalCell(unsigned sfi, unsigned i) const
{
#if 0
	if (sfi >= stack.size()) {
	  std::cout << "sfi=" << sfi << " i=" <<  i << std::endl;
	  for (unsigned i = 0; i < stack.size(); i++) {
	      std::cout << " " << stack[i].kf->function->getNameStr() << std::endl;

	  }
	  exit(1);
	}
#endif
	assert(sfi < stack.size());
	const StackFrame& sf = stack[sfi];
	assert(i < sf.kf->numRegisters);
	return sf.locals[i];
}

void ExecutionState::write(
	ObjectState* object, ref<Expr> offset, ref<Expr> value)
{
	object->write(offset, value);
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
{
	return stack.back().caller;
}

void ExecutionState::copy(
	ObjectState* os, const ObjectState* reallocFrom, unsigned count)
{
	for (unsigned i=0; i<count; i++) {
		write(os, i, read8(reallocFrom, i));
	}
}

void ExecutionState::dumpStack(std::ostream& os)
{
  unsigned idx = 0;
  const KInstruction *target = prevPC;
  foreach (it, stack.rbegin(), stack.rend())
  {
    StackFrame &sf = *it;
    Function *f = sf.kf->function;
    const InstructionInfo &ii = *target->info;
    os << "\t#" << idx++
        << " " << std::setw(8) << std::setfill('0') << ii.assemblyLine
        << " in " << f->getNameStr() << " (";
    // Yawn, we could go up and print varargs if we wanted to.
    unsigned index = 0;
    foreach (ai, f->arg_begin(), f->arg_end())
    {
      if (ai!=f->arg_begin()) os << ", ";

      os << ai->getNameStr();
      // XXX should go through function
      //ref<Expr> value = sf.locals[sf.kf->getArgRegister(index++)].value;
      ref<Expr> value = getLocalCell(stack.size() - idx, sf.kf->getArgRegister(index++)).value;
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

std::ostream &klee::operator<<(std::ostream &os, const MemoryMap &mm) {
  os << "{";
  MemoryMap::iterator it = mm.begin();
  MemoryMap::iterator ie = mm.end();
  if (it!=ie) {
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

bool ExecutionState::merge(const ExecutionState &b) {
  if (DebugLogStateMerge)
    std::cerr << "-- attempting merge of A:"
               << this << " with B:" << &b << "--\n";
  if (pc != b.pc)
    return false;

  // XXX is it even possible for these to differ? does it matter? probably
  // implies difference in object states?
  if (symbolics!=b.symbolics)
    return false;

  {
    std::vector<StackFrame>::const_iterator itA = stack.begin();
    std::vector<StackFrame>::const_iterator itB = b.stack.begin();
    while (itA!=stack.end() && itB!=b.stack.end()) {
      // XXX vaargs?
      if (itA->caller!=itB->caller || itA->kf!=itB->kf)
        return false;
      ++itA;
      ++itB;
    }
    if (itA!=stack.end() || itB!=b.stack.end())
      return false;
  }

  std::set< ref<Expr> > aConstraints(constraints.begin(), constraints.end());
  std::set< ref<Expr> > bConstraints(b.constraints.begin(),
                                     b.constraints.end());
  std::set< ref<Expr> > commonConstraints, aSuffix, bSuffix;
  std::set_intersection(aConstraints.begin(), aConstraints.end(),
                        bConstraints.begin(), bConstraints.end(),
                        std::inserter(commonConstraints, commonConstraints.begin()));
  std::set_difference(aConstraints.begin(), aConstraints.end(),
                      commonConstraints.begin(), commonConstraints.end(),
                      std::inserter(aSuffix, aSuffix.end()));
  std::set_difference(bConstraints.begin(), bConstraints.end(),
                      commonConstraints.begin(), commonConstraints.end(),
                      std::inserter(bSuffix, bSuffix.end()));
  if (DebugLogStateMerge) {
    std::cerr << "\tconstraint prefix: [";
    foreach (it, commonConstraints.begin(), commonConstraints.end()) {
	  std::cerr << *it << ", ";
    }
    std::cerr << "]\n";
    std::cerr << "\tA suffix: [";
    for (std::set< ref<Expr> >::iterator it = aSuffix.begin(),
           ie = aSuffix.end(); it != ie; ++it)
      std::cerr << *it << ", ";
    std::cerr << "]\n";
    std::cerr << "\tB suffix: [";
    for (std::set< ref<Expr> >::iterator it = bSuffix.begin(),
           ie = bSuffix.end(); it != ie; ++it)
      std::cerr << *it << ", ";
    std::cerr << "]\n";
  }

  // We cannot merge if addresses would resolve differently in the
  // states. This means:
  //
  // 1. Any objects created since the branch in either object must
  // have been free'd.
  //
  // 2. We cannot have free'd any pre-existing object in one state
  // and not the other

  if (DebugLogStateMerge) {
    std::cerr << "\tchecking object states\n";
    std::cerr << "A: " << addressSpace.objects << "\n";
    std::cerr << "B: " << b.addressSpace.objects << "\n";
  }

  std::set<const MemoryObject*> mutated;
  MemoryMap::iterator ai = addressSpace.objects.begin();
  MemoryMap::iterator bi = b.addressSpace.objects.begin();
  MemoryMap::iterator ae = addressSpace.objects.end();
  MemoryMap::iterator be = b.addressSpace.objects.end();
  for (; ai!=ae && bi!=be; ++ai, ++bi) {
    if (ai->first != bi->first) {
      if (DebugLogStateMerge) {
        if (ai->first < bi->first) {
          std::cerr << "\t\tB misses binding for: " << ai->first->id << "\n";
        } else {
          std::cerr << "\t\tA misses binding for: " << bi->first->id << "\n";
        }
      }
      return false;
    }
    if (ai->second != bi->second) {
      if (DebugLogStateMerge)
        std::cerr << "\t\tmutated: " << ai->first->id << "\n";
      mutated.insert(ai->first);
    }
  }
  if (ai!=ae || bi!=be) {
    if (DebugLogStateMerge)
      std::cerr << "\t\tmappings differ\n";
    return false;
  }

  // merge stack

  ref<Expr> inA = ConstantExpr::alloc(1, Expr::Bool);
  ref<Expr> inB = ConstantExpr::alloc(1, Expr::Bool);
  foreach (it, aSuffix.begin(), aSuffix.end()) {
    inA = AndExpr::create(inA, *it);
  }
  foreach (it, bSuffix.begin(), bSuffix.end()) {
    inB = AndExpr::create(inB, *it);
  }

  // XXX should we have a preference as to which predicate to use?
  // it seems like it can make a difference, even though logically
  // they must contradict each other and so inA => !inB

  for (unsigned sfi=0; sfi < stack.size(); sfi++) {
    for (unsigned i=0; i< stack[sfi].kf->numRegisters; i++) {
      ref<Expr> &av = getLocalCell(sfi, i).value;
      const ref<Expr> &bv = b.getLocalCell(sfi, i).value;
      if (av.isNull() || bv.isNull()) {
        // if one is null then by implication (we are at same pc)
        // we cannot reuse this local, so just ignore
      } else {
        av = SelectExpr::create(inA, av, bv);
      }
    }
  }

  foreach (it, mutated.begin(), mutated.end()) {
    const MemoryObject *mo = *it;
    const ObjectState *os = addressSpace.findObject(mo);
    const ObjectState *otherOS = b.addressSpace.findObject(mo);
    assert(os && !os->readOnly &&
           "objects mutated but not writable in merging state");
    assert(otherOS);

    ObjectState *wos = addressSpace.getWriteable(mo, os);
    for (unsigned i=0; i<mo->size; i++) {
      //ref<Expr> av = wos->read8(i);
      //ref<Expr> bv = otherOS->read8(i);
      //wos->write(i, SelectExpr::create(inA, av, bv));
      ref<Expr> av = read8(wos, i);
      ref<Expr> bv = b.read8(otherOS, i);
      write(wos, i, SelectExpr::create(inA, av, bv));
    }
  }

  constraints = ConstraintManager();
  foreach (it, commonConstraints.begin(), commonConstraints.end()) {
    constraints.addConstraint(*it);
  }
  constraints.addConstraint(OrExpr::create(inA, inB));

  return true;
}

void ExecutionState::bindArgument(
	KFunction *kf, unsigned index, ref<Expr> value)
{
    writeLocalCell(stack.size() - 1, kf->getArgRegister(index), value);
}

void ExecutionState::bindLocal(KInstruction* target, ref<Expr> value)
{
    writeLocalCell(stack.size() - 1, target->dest, value);
}

void ExecutionState::transferToBasicBlock(
	BasicBlock *dst,
	BasicBlock *src)
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
  KFunction *kf = stack.back().kf;
  unsigned entry = kf->basicBlockEntry[dst];
  pc = &kf->instructions[entry];
  if (pc->inst->getOpcode() == Instruction::PHI) {
    PHINode *first = static_cast<PHINode*>(pc->inst);
    incomingBBIndex = first->getBasicBlockIndex(src);
  }
}

ObjectState* ExecutionState::bindMemObj(
	const MemoryObject *mo,
	const Array *array)
{
	ObjectState *os;
	os = (array) ? new ObjectState(mo, array) : new ObjectState(mo);
	bindObject(mo, os);
	return os;
}

ObjectState* ExecutionState::bindStackMemObj(
	const MemoryObject *mo,
	const Array *array)
{
	ObjectState* os;

	os = bindMemObj(mo, array);

	// Its possible that multiple bindings of the same mo in the state
	// will put multiple copies on this list, but it doesn't really
	// matter because all we use this list for is to unbind the object
	// on function return.
	stack.back().addAlloca(mo);
	return os;
}

KFunction* ExecutionState::getCurrentKFunc(void) const
{
	return stack.back().kf;
}

void ExecutionState::trackBranch(int condIndex, int asmLine)
{
	// only track NON-internal branches
	if (replayBranchIterator != branchDecisionsSequence.end())
  		return;

	branchDecisionsSequence.push_back(
		condIndex,
		asmLine);

	replayBranchIterator = branchDecisionsSequence.end();
}

ExecutionState* ExecutionState::createReplay(
	ExecutionState& initialState,
	const ReplayPathType& replayPath)
{
	ExecutionState* newState;
	
	newState = new ExecutionState(initialState);
	foreach (it2, replayPath.begin(), replayPath.end()) {
		newState->branchDecisionsSequence.push_back(*it2);
	}
	newState->replayBranchIterator = newState->branchDecisionsSequence.begin();
	newState->ptreeNode->data = 0;
	newState->isReplay = true;

	return newState;
}

bool ExecutionState::isReplayDone(void) const
{
	return (replayBranchIterator == branchDecisionsSequence.end());
}

unsigned ExecutionState::stepReplay(void)
{
#ifdef INCLUDE_INSTR_ID_IN_PATH_INFO
    assert (prevPC->info->assemblyLine == (*replayBranchIterator).second &&
      "branch instruction IDs do not match");
#endif
    unsigned targetIndex = (*replayBranchIterator).first;
    ++replayBranchIterator;
    return targetIndex;
}

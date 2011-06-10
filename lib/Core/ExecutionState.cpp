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

#include "StateRecord.h"
#include "Memory.h"
#include "OpenfdRegistry.h"

#include "llvm/Function.h"
#include "llvm/Support/CommandLine.h"

#include <iostream>
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
  : symOffArrayAlloc(0),
    prunepoint(0),
    pruned(false),
    rec(0),
    fakeState(false),
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
  : symOffArrayAlloc(0),
    prunepoint(0),
    pruned(false),
    rec(0),
    fakeState(true),
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

ExecutionState::~ExecutionState() {
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

	if (rec) rec->split(this, newState);

	return newState;
}

void ExecutionState::bindObject(const MemoryObject *mo, ObjectState *os)
{
	if (!rec) {
		addressSpace.bindObject(mo, os);
		return ;
	}

	if (mo->mallocKey.allocSite) {
		assert (
		(mallocKeyMap.find(mo->mallocKey) == mallocKeyMap.end()) ||
		(mallocKeyMap[mo->mallocKey] == mo));
		mallocKeyMap[mo->mallocKey] = mo;
	}

	//StateRecord* existing = mallocKeyAlloc[mo->mallocKey];
	//assert(!existing);
	mallocKeyAlloc[mo->mallocKey] = os->allocRec;
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
    addressSpace.unbindObject(*it);
  stack.pop_back();
}

///

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

	if (!rec) return sf.locals[i];

	ref<Expr> e = sf.locals[i].value;
	StackWrite* sw = sf.locals[i].stackWrite;
	rec->stackRead(sw);

	if (isa<ConstantExpr > (e ))
		return sf.locals[i];

	std::vector<ref<ReadExpr> > usedReadExprs;

	findReads(e, true, usedReadExprs);

	foreach (it, usedReadExprs.begin(), usedReadExprs.end()) {
		ref<ReadExpr> re = *it;
		rec->arrayRead(this, re);
	}

	return sf.locals[i];
}

void ExecutionState::addConstraint(ref<Expr> constraint)
{
  /*  if (rec) {
      std::vector<ref<ReadExpr> > usedReadExprs;

      findReads(constraint, true, usedReadExprs);

      for (std::vector<ref<ReadExpr> >::iterator it = usedReadExprs.begin();
              it != usedReadExprs.end(); ++it) {
        ref<ReadExpr> re = *it;
        if (ConstantExpr* ce = dyn_cast<ConstantExpr > (re->index)) {
          unsigned offset = (uint8_t) ce->getZExtValue();
          como2cn[MallocKeyOffset(re->updates.root->mallocKey, offset)].insert(constraint);
        }
        else {
          somo2cn[re->updates.root->mallocKey].insert(constraint);
        }
      }
    }*/

    constraints.addConstraint(constraint);
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
	object->write(offset, value, rec);
	if (!rec) return;
	if (!object->wasSymOffObjectWrite) return;
	object->wasSymOffObjectWrite = false;
	rec->symOffObjectWrite(object);        
}

void ExecutionState::writeLocalCell(unsigned sfi, unsigned i, ref<Expr> value)
{
	assert(sfi < stack.size());
	const StackFrame& sf = stack[sfi];
	KFunction* kf = sf.kf;
	assert(i < kf->numRegisters);

	if (rec) {
		sf.locals[i].stackWrite = rec->stackWrite(
			kf, sf.call, sfi, i, value);
	}
	sf.locals[i].value = value;
}

const ObjectState* ExecutionState::getObjectState(const MallocKey& mk)
{
      MallocKeyMap::iterator it = mallocKeyMap.find(mk);
      if (it == mallocKeyMap.end()) return NULL;

      const MemoryObject* mo = it->second;
      return addressSpace.findObject(mo);
}

KInstIterator ExecutionState::getCaller(void) const
{
	return stack.back().caller;
}

void ExecutionState::copy(
	ObjectState* os, const ObjectState* reallocFrom, unsigned count)
{
	std::set<DependenceNode*> allReads;
	std::set<DependenceNode*> initialReads;
	if (rec) {
		allReads.insert(rec->curreads.begin(), rec->curreads.end());
		initialReads.insert(rec->curreads.begin(), rec->curreads.end());
	}

	for (unsigned i=0; i<count; i++) {
		if (rec) {
			rec->curreads.clear();
			rec->curreads.insert(
				initialReads.begin(), initialReads.end());
		}
		write(os, i, read8(reallocFrom, i));
		if (rec) {
			allReads.insert(
				rec->curreads.begin(), rec->curreads.end());
		}
	}

	if (rec) {
		rec->curreads.clear();
		rec->curreads.insert(allReads.begin(), allReads.end());
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

/***/

ExecutionTraceEvent::ExecutionTraceEvent(ExecutionState& state,
                                         KInstruction* ki)
  : consecutiveCount(1)
{
  file = ki->info->file;
  line = ki->info->line;
  funcName = state.stack.back().kf->function->getName();
  stackDepth = state.stack.size();
}

bool ExecutionTraceEvent::ignoreMe() const {
  // ignore all events occurring in certain pesky uclibc files:
  if (file.find("libc/stdio/") != std::string::npos) {
    return true;
  }

  return false;
}

void ExecutionTraceEvent::print(std::ostream &os) const {
  os.width(stackDepth);
  os << ' ';
  printDetails(os);
  os << ' ' << file << ':' << line << ':' << funcName;
  if (consecutiveCount > 1)
    os << " (" << consecutiveCount << "x)\n";
  else
    os << '\n';
}


bool ExecutionTraceEventEquals(ExecutionTraceEvent* e1, ExecutionTraceEvent* e2) {
  // first see if their base class members are identical:
  if (!((e1->file == e2->file) &&
        (e1->line == e2->line) &&
        (e1->funcName == e2->funcName)))
    return false;

  // fairly ugly, but i'm no OOP master, so this is the way i'm
  // doing it for now ... lemme know if there's a cleaner way:
  BranchTraceEvent* be1 = dynamic_cast<BranchTraceEvent*>(e1);
  BranchTraceEvent* be2 = dynamic_cast<BranchTraceEvent*>(e2);
  if (be1 && be2) {
    return ((be1->trueTaken == be2->trueTaken) &&
            (be1->canForkGoBothWays == be2->canForkGoBothWays));
  }

  // don't tolerate duplicates in anything else:
  return false;
}


void BranchTraceEvent::printDetails(std::ostream &os) const {
  os << "BRANCH " << (trueTaken ? "T" : "F") << ' ' <<
        (canForkGoBothWays ? "2-way" : "1-way");
}

void ExecutionTraceManager::addEvent(ExecutionTraceEvent* evt) {
  // don't trace anything before __user_main, except for global events
  if (!hasSeenUserMain) {
    if (evt->funcName == "__user_main") {
      hasSeenUserMain = true;
    }
    else if (evt->funcName != "global_def") {
      return;
    }
  }

  // custom ignore events:
  if (evt->ignoreMe())
    return;

  if (events.size() > 0) {
    // compress consecutive duplicates:
    ExecutionTraceEvent* last = events.back();
    if (ExecutionTraceEventEquals(last, evt)) {
      last->consecutiveCount++;
      return;
    }
  }

  events.push_back(evt);
}

void ExecutionTraceManager::printAllEvents(std::ostream &os) const {
  for (unsigned i = 0; i != events.size(); ++i)
    events[i]->print(os);
}

/***/

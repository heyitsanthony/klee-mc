#include "klee/ExecutionState.h"
#include "static/Sugar.h"
#include "klee/Internal/Module/KModule.h"
#include <llvm/Support/CommandLine.h>

using namespace llvm;
using namespace klee;

namespace {
  cl::opt<bool>
  DebugLogStateMerge("debug-log-state-merge");
}

bool ExecutionState::merge(const ExecutionState &b)
{
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



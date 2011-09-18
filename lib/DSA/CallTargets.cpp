//=- lib/Analysis/IPA/CallTargets.cpp - Resolve Call Targets --*- C++ -*-=====//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass uses DSA to map targets of all calls, and reports on if it
// thinks it knows all targets of a given call.
//
// Loop over all callsites, and lookup the DSNode for that site.  Pull the
// Functions from the node as callees.
// This is essentially a utility pass to simplify later passes that only depend
// on call sites and callees to operate (such as a devirtualizer).
//
//===----------------------------------------------------------------------===//

#include "llvm/Module.h"
#include "llvm/Instructions.h"
#include "static/dsa/DataStructure.h"
#include "static/dsa/DSGraph.h"
#include "static/dsa/CallTargets.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Constants.h"
#include <static/Sugar.h>
#include <ostream>
using namespace llvm;

namespace {
  STATISTIC (DirCall, "Number of direct calls");
  STATISTIC (IndCall, "Number of indirect calls");
  STATISTIC (CompleteInd, "Number of complete indirect calls");
  STATISTIC (CompleteEmpty, "Number of complete empty calls");

  RegisterPass<CallTargetFinder> X("calltarget","Find Call Targets (uses DSA)");
}

char CallTargetFinder::ID;

void CallTargetFinder::findIndTargets(Module &M)
{
  EQTDDataStructures* T = &getAnalysis<EQTDDataStructures>();
  foreach (I, M.begin(), M.end()) {
    if (I->isDeclaration()) continue;

    foreach (F, I->begin(), I->end())
      foreach (B, F->begin(), F->end()) {
        if (!isa<CallInst>(B) && !isa<InvokeInst>(B)) continue;

        CallSite cs(B);
        AllSites.push_back(cs);
        Function* CF = cs.getCalledFunction();
        // If the called function is casted from one function type to another, peer
        // into the cast instruction and pull out the actual function being called.
        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(cs.getCalledValue()))
          if (CE->getOpcode() == Instruction::BitCast &&
              isa<Function>(CE->getOperand(0)))
            CF = cast<Function>(CE->getOperand(0));

        if (CF) {
          ++DirCall;
          IndMap[cs].push_back(cs.getCalledFunction());
          CompleteSites.insert(cs);
          continue;
        }

        if (isa<ConstantPointerNull>(cs.getCalledValue())) {
            ++DirCall;
            CompleteSites.insert(cs);
        } else {
          IndCall++;
          DSNode* N = T->getDSGraph(*cs.getCaller())
            ->getNodeForValue(cs.getCalledValue()).getNode();
          assert (N && "CallTarget: findIndTargets: No DSNode!\n");
          N->addFullFunctionList(IndMap[cs]);
          if (N->isCompleteNode() && IndMap[cs].size()) {
            CompleteSites.insert(cs);
            ++CompleteInd;
          }
          if (N->isCompleteNode() && !IndMap[cs].size()) {
            ++CompleteEmpty;
#if 0
            DEBUG(errs() << "Call site empty: '"
		< cs.getInstruction()->getName()
		< "' In '"
		< cs.getInstruction()->getParent()->getParent()->getName()
		< "'\n");
#endif
          }
        }
      }
  }
}

void CallTargetFinder::print(std::ostream &O, const Module *M) const
{
  O << "[* = incomplete] CS: func list\n";
  foreach (ii, IndMap.begin(), IndMap.end()) {
    if (!ii->first.getCalledFunction()) { //only print indirect
      if (!isComplete(ii->first)) {
        O << "* ";
        CallSite cs = ii->first;
        cs.getInstruction()->dump();
        O << cs.getInstruction()->getParent()->getParent()->getNameStr() << " "
          << cs.getInstruction()->getNameStr() << " ";
      }
      O << ii->first.getInstruction() << ":";
      for (std::vector<const Function*>::const_iterator i = ii->second.begin(),
             e = ii->second.end(); i != e; ++i) {
        O << " " << (*i)->getNameStr();
      }
      O << "\n";
    }
  }
}

bool CallTargetFinder::runOnModule(Module &M) {
  findIndTargets(M);
  return false;
}

void CallTargetFinder::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<EQTDDataStructures>();
}

std::vector<const Function*>::iterator CallTargetFinder::begin(CallSite cs) {
  return IndMap[cs].begin();
}

std::vector<const Function*>::iterator CallTargetFinder::end(CallSite cs) {
  return IndMap[cs].end();
}

bool CallTargetFinder::isComplete(CallSite cs) const {
  return CompleteSites.find(cs) != CompleteSites.end();
}

std::list<CallSite>::iterator CallTargetFinder::cs_begin() {
  return AllSites.begin();
}

std::list<CallSite>::iterator CallTargetFinder::cs_end() {
  return AllSites.end();
}

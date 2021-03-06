//===- Basic.cpp ----------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementation of the basic data structure analysis pass. It simply assumes
// that all pointers can points to all possible locations.
//
//===----------------------------------------------------------------------===//

#include "static/dsa/DataStructure.h"
#include "static/dsa/DSGraph.h"

#include <llvm/IR/Module.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/TypeBuilder.h>
#include <llvm/IR/InstIterator.h>
#include "static/Sugar.h"

using namespace llvm;

static RegisterPass<BasicDataStructures>
X("dsa-basic", "Basic Data Structure Analysis(No Analysis)");

char BasicDataStructures::ID = 0;

bool BasicDataStructures::runOnModule(Module &M) {
  init(&getAnalysis<DataLayout>());

  //
  // Create a void pointer type.  This is simply a pointer to an 8 bit value.
  //
  IntegerType * IT = IntegerType::getInt8Ty(getGlobalContext());
  PointerType * VoidPtrTy = PointerType::getUnqual(IT);

  DSNode * GVNodeInternal = new DSNode(VoidPtrTy, GlobalsGraph);
  DSNode * GVNodeExternal = new DSNode(VoidPtrTy, GlobalsGraph);
  foreach (I, M.global_begin(),M.global_end()) {
    if (I->isDeclaration()) {
      GlobalsGraph->getNodeForValue(&*I).mergeWith(GVNodeExternal);
    } else {
      GlobalsGraph->getNodeForValue(&*I).mergeWith(GVNodeInternal);
    }
  }

  GVNodeInternal->foldNodeCompletely();
  GVNodeInternal->maskNodeTypes(DSNode::IncompleteNode);

  GVNodeExternal->foldNodeCompletely();

  // Next step, iterate through the nodes in the globals graph, unioning
  // together the globals into equivalence classes.
  formGlobalECs();

  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    if (!F->isDeclaration()) {
      DSGraph* G = new DSGraph(GlobalECs, getDataLayout(), GlobalsGraph);
      DSNode * Node = new DSNode(VoidPtrTy, G);
          
      if (!F->hasInternalLinkage())
        Node->setExternalMarker();

      // Create scalar nodes for all pointer arguments...
      for (Function::arg_iterator I = F->arg_begin(), E = F->arg_end();
          I != E; ++I) {
        if (isa<PointerType>(I->getType())) {
          G->getNodeForValue(&*I).mergeWith(Node);
        }
      }

      foreach (I, inst_begin(F), inst_end(F)) {
        G->getNodeForValue(&*I).mergeWith(Node);
      }

      Node->foldNodeCompletely();
      Node->maskNodeTypes(DSNode::IncompleteNode);

      setDSGraph(*F, G);
    }
  }
 
  return false;
}

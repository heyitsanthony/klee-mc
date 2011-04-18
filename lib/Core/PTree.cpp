//===-- PTree.cpp ---------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PTree.h"

#include <klee/Expr.h>
#include <klee/util/ExprPPrinter.h>

#include <vector>
#include <iostream>
#include <fstream>

using namespace klee;

/* *** */

PTree::PTree(const data_type &_root) : root(new Node(0, _root)) {
}

PTree::~PTree() {
  assert(root);
  assert(!root->data);
  assert(!root->left && !root->right);
  delete root;
}

void PTree::checkRep() {
  return;
  std::vector<PTree::Node*> stack;
  stack.push_back(root);
  while (!stack.empty()) {
    PTree::Node *n = stack.back();
    stack.pop_back();

    if (n->ignore) {
      if (n->parent) {
        if ((!n->parent->left || n->parent->left->ignore) && (!n->parent->right || n->parent->right->ignore)) {
          if (!n->parent->ignore) {
            std::cout << "PARENT=" << n->parent->id << std::endl;
            if (n->parent->left) {
              std::cout << "PARENT HAS LEFT=" << n->parent->left->id << std::endl;
              if (n->parent->left->ignore)
                std::cout << "PARENT HAS LEFT IGNORE" << std::endl;
            }
            if (n->parent->right) {
              std::cout << "PARENT HAS RIGHT" << n->parent->right->id << std::endl;
              if (n->parent->right->ignore)
                std::cout << "PARENT HAS RIGHT IGNORE" << std::endl;
            }

            dump("process");
            assert(false);
          }
        }
      }
    }

    if (n->left)
      stack.push_back(n->left);
    if (n->right)
      stack.push_back(n->right);
  }
}

std::pair<PTreeNode*, PTreeNode*>
PTree::split(Node *n,
        const data_type &leftData,
        const data_type &rightData) {
  checkRep();
  assert(n && !n->left && !n->right);
  n->left = new Node(n, leftData);
  n->right = new Node(n, rightData);
  checkRep();
  return std::make_pair(n->left, n->right);
}

void PTree::remove(Node *n) {
  checkRep();
  assert(!n->left && !n->right);
  //dump("process1");
  do {
    Node *p = n->parent;

    assert(p);
    assert(n == p->left || n == p->right);
    if (n == p->left) {
      p->left = 0;
      p->sumLeft.assign(NumWeights, false);
    } else {
      p->right = 0;
      p->sumRight.assign(NumWeights, false);
    }
    delete n;

    n = p;
  } while (n->parent && !n->left && !n->right);
  //dump("process2");


  Node* k = n;
  //std::cout << "START: " << k->id << std::endl;
  while (k &&
          ((k->left && k->left->ignore) || (k->right && k->right->ignore)) &&
          ((!k->left || k->left->ignore) && (!k->right || k->right->ignore))) {
    //std::cout << "ITER: " << k->id << std::endl;
    k->ignore = true;
    k = k->parent;
  }

  propagateSumsUp(n);

  checkRep();
}

void PTree::update(Node *n, Weights index, bool sum) {  
  if (Node * p = n->parent) {
    std::vector<bool> &curSum = ((n == p->left) ? p->sumLeft : p->sumRight);
    curSum[index] = sum;

    curSum[WeightAnd] = true;
    curSum[WeightAndCompact] = true;
    for (unsigned i = WeightAnd; i < NumWeights; i++) {
      curSum[WeightAnd] = curSum[WeightAnd] & curSum[i];

      if (i != WeightCompact)
        curSum[WeightAndCompact] = curSum[WeightAndCompact] & curSum[i];
    }

    propagateSumsUp(p);
  }  
}

void PTree::dump(const std::string& n) {

  std::ofstream os;
  std::string name = n + ".dot";
  os.open(name.c_str());
  dump(os);

  os.flush();
  os.close();
}

void PTree::dump(std::ostream &os) {
  ExprPPrinter *pp = ExprPPrinter::create(os);
  pp->setNewline("\\l");
  os << "digraph G {\n";
  os << "\tsize=\"10,7.5\";\n";
  os << "\tratio=fill;\n";
  os << "\trotate=90;\n";
  os << "\tcenter = \"true\";\n";
  os << "\tnode [style=\"filled\",width=.1,height=.1,fontname=\"Terminus\"]\n";
  os << "\tedge [arrowsize=.3]\n";
  std::vector<PTree::Node*> stack;
  stack.push_back(root);
  while (!stack.empty()) {
    PTree::Node *n = stack.back();
    stack.pop_back();
    os << "\tn" << n << " [label=\"n" << n->id << "\"";
    //if (n->data)
    //os << ",fillcolor=green";
    if (n->ignore)
      os << ",fillcolor=red";
    os << "];\n";
    if (n->left) {
      os << "\tn" << n << " -> n" << n->left << ";\n";
      stack.push_back(n->left);
    }
    if (n->right) {

      os << "\tn" << n << " -> n" << n->right << ";\n";
      stack.push_back(n->right);
    }
  }
  os << "}\n";
  delete pp;
}

void PTree::propagateSumsUp(Node *n) {
  while (Node * p = n->parent) {
    std::vector<bool> sums(PTree::NumWeights, false);
    std::vector<bool> &curSum = ((n == p->left) ? p->sumLeft : p->sumRight);

    for (unsigned i = 0; i < PTree::NumWeights; i++)
      sums[i] = n->sumLeft[i] | n->sumRight[i];

    // avoid propagating up if we already have the same weights
    if (curSum[WeightAnd] == sums[WeightAnd]
            && curSum[WeightAndCompact] == sums[WeightAndCompact])
      break;

    curSum = sums;
    n = p;
  }
}

unsigned PTreeNode::idCount = 0;

PTreeNode::PTreeNode(PTreeNode *_parent,
        ExecutionState *_data)
: ignore(false), parent(_parent),
left(0),
right(0),
data(_data),
sumLeft(PTree::NumWeights, true),
sumRight(PTree::NumWeights, true), id(idCount++) {

}

PTreeNode::~PTreeNode() {
}


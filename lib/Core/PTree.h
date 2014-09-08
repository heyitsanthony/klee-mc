//===-- PTree.h -------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef __UTIL_PTREE_H__
#define __UTIL_PTREE_H__

#include <klee/Expr.h>

#include <utility>
#include <cassert>
#include <iostream>

namespace klee
{
class ExecutionState;
class PTreeNode;

/* A tree that tracks probabilities; not too great */

class PTree
{
	typedef ExecutionState* data_type;

public:
	enum Weights {
	      WeightAnd = 0, // logical AND of weights
	      WeightAndNoCompact, // logical AND of weights (except WeightCompact)
	      WeightCompact, // 0 = compact, 1 = non-compact
	      WeightRunnable, // 0 = not runnable, 1 = runnable
	      NumWeights,
	      FirstVarWeight = WeightCompact
	};

	PTreeNode *root;

	PTree(const data_type &_root);
	~PTree();

	void remove(PTreeNode *n);

	void dump(std::ostream &os) const;
	void dump(const std::string& n) const;
	ExecutionState* removeState(ExecutionState* es);
	void removeRoot(ExecutionState* es);

	void splitStates(PTreeNode* n, ExecutionState* a, ExecutionState* b);
	void updateReplacement(ExecutionState* ns, ExecutionState* es);

	bool isRoot(ExecutionState* es) const;
private:
	std::pair<PTreeNode*,PTreeNode*> split(
		PTreeNode *n,
		const data_type &leftData,
		const data_type &rightData);
};

class PTreeNode
{
	friend class PTree;
public:
	llvm::SmallVector<PTreeNode*, 2>	children;
	llvm::SmallVector<std::vector<bool>, 2>	sums;

	void update(PTree::Weights index, bool sum);
	double getProbability() const;

	PTreeNode(PTreeNode *_parent, ExecutionState *_data)
	: parent(_parent), data(_data) { }
	~PTreeNode() { }

	PTreeNode* getParent(void) const { return parent; }
	ExecutionState* getData(void) const { return data; }
	void markReplay(void) { data = 0; }
private:
	void propagateSumsUp();

	PTreeNode	*parent;
	ExecutionState	*data;
};
}

#endif

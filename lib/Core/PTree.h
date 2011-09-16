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
class ExeStateManager;

class PTree
{
	typedef ExecutionState* data_type;

public:
	enum Weights {
		WeightAnd = 0, // logical AND of weights
		WeightAndCompact, // logical AND of weights (except WeightCompact)
		WeightCompact, // 0 = compact, 1 = non-compact
		//      WeightRunning, // 0 = running, 1 = not running
		NumWeights
	};

	PTreeNode *root;

	PTree(const data_type &_root);
	~PTree();

	std::pair<PTreeNode*,PTreeNode*> split(
		PTreeNode *n,
		const data_type &leftData,
		const data_type &rightData);

	void remove(PTreeNode *n);
	void update(PTreeNode *n, Weights index, bool sum);

	void dump(std::ostream &os);
	void dump(const std::string& n);
	void checkRep();
	ExecutionState* removeState(
		ExeStateManager* stateManager, ExecutionState* es);
	void removeRoot(ExeStateManager* stateManager, ExecutionState* es);

	void splitStates(PTreeNode* n, ExecutionState* a, ExecutionState* b);

private:
	void propagateSumsUp(PTreeNode *n);
};

class PTreeNode
{
friend class PTree;
public:
	bool ignore;
	PTreeNode *parent, *left, *right;
	ExecutionState *data;
	std::vector<bool> sumLeft, sumRight;
	unsigned id;
	static unsigned idCount;
private:
	PTreeNode(PTreeNode *_parent, ExecutionState *_data);
	~PTreeNode();
};
}

#endif

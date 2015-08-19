//===-- PTree.cpp ---------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PTree.h"
#include "ExeStateManager.h"
#include <klee/ExecutionState.h>

#include <klee/Expr.h>
#include <klee/util/ExprPPrinter.h>

#include <vector>
#include <iostream>
#include <fstream>

using namespace klee;

PTree::PTree(const data_type &_root)
: root(std::make_shared<PTreeNode>(nullptr, _root))
{}

PTree::~PTree()
{
	assert(root);
	assert(!root->data);
	assert(root->children.empty());
}

std::pair<shared_ptnode, shared_ptnode>
PTree::split(
	shared_ptnode &n,
	const data_type &leftData,
	const data_type &rightData)
{
	assert(n && n->children.empty());
	n->children.resize(2);
	n->children[0] = std::make_shared<PTreeNode>(n, leftData);
	n->children[1] = std::make_shared<PTreeNode>(n, rightData);
	n->sums.assign(
		n->children.size(),
		std::vector<bool>(NumWeights, true));
	return std::make_pair(n->children[0], n->children[1]);
}

bool PTree::isRoot(ExecutionState* es) const { return es->ptreeNode == root; }

void PTree::updateReplacement(ExecutionState* ns, ExecutionState *es)
{
	// replace the placeholder state in the process tree
	ns->ptreeNode = es->ptreeNode;
	ns->ptreeNode->data = ns;
	ns->ptreeNode->update(WeightCompact, !ns->isCompact());
}

void PTree::removeRoot(ExecutionState* es)
{
	assert (isRoot(es));
	delete root->data;
	root->data = nullptr;
}

void PTree::remove(shared_ptnode& n)
{
	assert(n->children.empty() && "Cannot remove interior node");

	while (n->parent && n->children.empty()) {
		auto p = n->parent;
		bool found = false;

		assert(p != NULL);

		for (unsigned i = 0; i < p->children.size(); i++) {
			if (n != p->children[i])
				continue;

			found = true;
			p->children.erase(&p->children[i]);
			p->sums.erase(&p->sums[i]);
			break;
		}

		assert(found && "Orphaned node detected");

		// collapse away nodes with a single child
		if (p->children.size() != 1) {
			// n may vanish here
			n = p;
			continue;
		}

		if (auto p2 = p->parent) {
			found = false;
			for (unsigned i = 0; i < p2->children.size(); i++) {
				if (p != p2->children[i])
					continue;

				found = true;
				p2->children[i] = p->children[0];
				p->children[0]->parent = p2;
				p2->sums[i] = p->sums[0];

				// p is orphaned, so replacement deletes it
				p = p2;

				break;
			}

			assert(found && "Orphaned node detected");
		} else {
			// 'p' is the root node
			root = p->children[0];
			root->parent = nullptr;
			p->children.clear();
			// p is orphaned, so replacement deletes it
			p = root;
		}

		// n may vanish here
		n = p;
	}

	assert (n);
	n->propagateSumsUp();
}

void PTreeNode::update(PTree::Weights index, bool sum)
{
	PTreeNode	*p = parent.get();
	unsigned	i;

	if (p == NULL)
		return;

	for (i = 0; i < p->children.size(); i++)
		if (p->children[i].get() == this)
			break;

	assert(i < p->children.size() && "Orphaned node detected");
	std::vector<bool> &curSum = p->sums[i];
	curSum[index] = sum;

	curSum[PTree::WeightAnd] = true;
	curSum[PTree::WeightAndNoCompact] = true;
	for(unsigned j = PTree::FirstVarWeight; j < PTree::NumWeights; j++) {
		curSum[PTree::WeightAnd] = curSum[PTree::WeightAnd] & curSum[j];
		if (j == PTree::WeightCompact)
			continue;

		curSum[PTree::WeightAndNoCompact] = 
			curSum[PTree::WeightAndNoCompact] & curSum[j];
	}

	p->propagateSumsUp();
}

void PTree::dump(const std::string& n) const
{
	std::ofstream os;
	std::string name = n + ".dot";
	os.open(name.c_str());
	dump(os);

	os.flush();
	os.close();
}

void PTree::dump(std::ostream &os) const
{
	ExprPPrinter		*pp;
	std::vector<PTreeNode*> stack;

	pp = ExprPPrinter::create(os);
	pp->setNewline("\\l");
	os << "digraph G {\n";
	os << "\tsize=\"10,7.5\";\n";
	os << "\tratio=fill;\n";
	os << "\trotate=90;\n";
	os << "\tcenter = \"true\";\n";
	os << 	"\tnode [style=\"filled\","
		"width=.1,height=.1,fontname=\"Terminus\"]\n";
	os << "\tedge [arrowsize=.3]\n";

	stack.push_back(root.get());
	while (!stack.empty()) {
		auto n = stack.back();
		stack.pop_back();
		os << "\tn" << n << " [label=\"\"";
		if (n->data)
			os << ",fillcolor=green";
		os << "];\n";
		for (unsigned i = 0; i < n->children.size(); i++) {
			os << "\tn" << n << " -> n" << n->children[i] << ";\n";
			stack.push_back(n->children[i].get());
		}
	}

	os << "}\n";
	delete pp;
}

void PTree::splitStates(shared_ptnode& n, ExecutionState* a, ExecutionState* b)
{
	assert(!n->data);
	auto res = split(n, a, b);

	a->ptreeNode = res.first;
	a->ptreeNode->update(PTree::WeightCompact, !a->isCompact());
	// update(a->ptreeNode, PTree::WeightRunning, !a->isRunning);
	b->ptreeNode = res.second;
	b->ptreeNode->update(WeightCompact, !b->isCompact());
	//  update(b->ptreeNode, PTree::WeightRunning, !b->isRunning);
}

void PTreeNode::propagateSumsUp(void)
{
	PTreeNode *n = this;

	while (auto p = n->parent.get()) {
		unsigned i;
		for (i = 0; i < p->children.size(); i++)
			if (n == p->children[i].get())
				break;
		assert(i < p->children.size() && "Orphaned node detected");

		std::vector<bool> sums(PTree::NumWeights, false);
		std::vector<bool> &curSum = p->sums[i];

		for (i = 0; i < n->children.size(); i++)
			for (unsigned j = 0; j < PTree::NumWeights; j++)
				sums[j] = sums[j] | n->sums[i][j];

		// avoid propagating up if we already have the same weights
		if (	curSum[PTree::WeightAnd] == sums[PTree::WeightAnd]
			&& 	curSum[PTree::WeightAndNoCompact] == 
				sums[PTree::WeightAndNoCompact])
		{
			break;
		}

		curSum = sums;
		n = p;
	}
}

double PTreeNode::getProbability() const
{
	double p = 1.;
	const PTreeNode *n = this;

	while (n->parent != NULL) {
		p /= (double) n->parent->children.size();
		n = n->parent.get();
	}

	return p;
}


//===-- Updates.cpp -------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "static/Sugar.h"
#include "llvm/ADT/StringExtras.h"
#include <iostream>
#include "klee/Expr.h"

#include <cassert>

using namespace klee;

UpdateNode::UpdateNode(
	const UpdateNode *_next,
	const ref<Expr> &_index,
	const ref<Expr> &_value)
: refCount(0)
, stpArray(0)
, next(_next)
, index(_index)
, value(_value)
{
	assert(	_value->getWidth() == Expr::Int8 &&
		"Update value should be 8-bit wide.");

	computeHash();

	if (next != NULL) {
		++next->refCount;
		size = 1 + next->size;
	} else
		size = 1;
}

extern "C" void vc_DeleteExpr(void*);

UpdateNode::~UpdateNode()
{
	// XXX gross
	if (stpArray)
		::vc_DeleteExpr(stpArray);
}

int UpdateNode::compare(const UpdateNode &b) const
{
	if (this == &b) return 0;

	if (int i = index.compare(b.index))
		return i;

	if (int v = value.compare(b.value))
	    return v;

	return 0;
}

Expr::Hash UpdateNode::computeHash()
{
	hashValue = index->hash() ^ value->hash();
	if (next != NULL)
		hashValue ^= next->hash();
	return hashValue;
}

unsigned UpdateList::totalUpdateLists = 0;

UpdateList::UpdateList(const ref<Array>& _root, const UpdateNode *_head)
: root(_root)
, hashValue(0)
, head(_head)
{
	if (head != NULL) ++head->refCount;
	totalUpdateLists++;
}

UpdateList::UpdateList(const UpdateList &b)
: root(b.root)
, hashValue(0)
, head(b.head)
{
	if (head != NULL) ++head->refCount;
	totalUpdateLists++;
}

UpdateList::~UpdateList()
{
	// We need to be careful and avoid recursion here. We do this in
	// cooperation with the private dtor of UpdateNode which does not
	// recursively free its tail.
	while (head != NULL && --head->refCount==0) {
		const UpdateNode *n = head->next;
		delete head;
		head = n;
	}
	totalUpdateLists--;
}

UpdateList &UpdateList::operator=(const UpdateList &b)
{
	if (&b == this) return *this;

	if (b.head) ++b.head->refCount;

	while (head && --head->refCount==0) {
		const UpdateNode *n = head->next;
		delete head;
		head = n;
	}

	root = const_cast<Array*>(b.root.get());
	head = b.head;
	hashValue = 0;

	return *this;
}

void UpdateList::extend(const ref<Expr> &index, const ref<Expr> &value)
{
	if (head != NULL)
		--head->refCount;
	head = new UpdateNode(head, index, value);
	++head->refCount;
	hashValue = 0;
}

int UpdateList::compare(const UpdateList &b) const
{
	if (this == &b) return 0;

	if (hash() != b.hash())
		return (hash() < b.hash()) ? -1 : 1;

	if (*root < *b.root)
		return -1;
	if (*b.root < *root)
		return 1;

	if (getSize() < b.getSize())
		return -1;

	if (getSize() > b.getSize())
		return 1;

/* XXX: 0 => super unsound but whatever */
#if 1
	// XXX build comparison into update, make fast
	const UpdateNode *an=head, *bn=b.head;
	for (; an && bn; an=an->next,bn=bn->next) {
		// exploit shared list structure
		if (an == bn)
			return 0;
		if (int res = an->compare(*bn))
			return res;
	}

	assert (!an && !bn);
#endif
	return 0;
}

Expr::Hash UpdateList::computeHash() const
{
	Expr::Hash	res;

	if (root.isNull())
		return ~0;

	if (root->mallocKey.allocSite) {
		res = root->mallocKey.hash();
	} else {
		//res = Expr::hashImpl(
		//root->name.c_str(), root->name.size(), 0);
		res = root->hash();
	}

	if (head != NULL) {
		uint32_t	head_hash = head->hash();
		res = Expr::hashImpl(&head_hash, 4, res);
	}

	return res;
}

UpdateList* UpdateList::fromUpdateStack(
	const Array	*old_root,
	std::stack<std::pair<ref<Expr>, ref<Expr> > >& updateStack)
{
	UpdateList				*newUpdates = NULL;
	std::vector< ref<ConstantExpr> >	constantValues;
	std::set<ref<Expr> >			seen_idx, dup_idx;

	static unsigned	id = 0;

	old_root->getConstantValues(constantValues);

	while (!updateStack.empty()) {
		ref<Expr> index = updateStack.top().first;
		ref<Expr> value = updateStack.top().second;

		if (seen_idx.count(index) == 0) {
			seen_idx.insert(index);
		} else {
			dup_idx.insert(index);
		}

		ConstantExpr *cIdx = dyn_cast<ConstantExpr>(index);
		ConstantExpr *cVal = dyn_cast<ConstantExpr>(value);

		// flush newly constant writes to constant array
		if (	cIdx && cVal &&
			newUpdates == NULL &&
			cIdx->getZExtValue() < constantValues.size())
		{
			uint64_t	idx_v = cIdx->getZExtValue();
			constantValues[idx_v] =	ref<ConstantExpr>(cVal);
			updateStack.pop();
			continue;
		}

		if (newUpdates == NULL) {
			ref<Array> newRoot;

			newRoot = Array::create(
				"simpl_arr"+llvm::utostr(++id),
				old_root->mallocKey,
				&constantValues[0],
				&constantValues[0]+constantValues.size());
			newRoot = Array::uniqueArray(newRoot);

			newUpdates = new UpdateList(newRoot, NULL);
		}
		newUpdates->extend(index, value);

		updateStack.pop();
	}

	// all-constant array
	if (newUpdates == NULL) {
		ref<Array> newRoot;

		if (constantValues.size() == 0)
			return NULL;

		newRoot = Array::create(
			"simpl_arr"+llvm::utostr(++id),
			old_root->mallocKey,
			&constantValues[0],
			&constantValues[0]+constantValues.size());
		newRoot = Array::uniqueArray(newRoot);
		newUpdates = new UpdateList(newRoot, NULL);
	}

	//std::cerr << "TOTAL UPDATELISTS: " << UpdateList::getCount() << '\n';
	//std::cerr << "TOTAL ARRAYS: " << Array::getNumArrays() << '\n';

	foreach (it, dup_idx.begin(), dup_idx.end())
		newUpdates->removeDups(*it);

	return newUpdates;
}


void UpdateList::removeDups(const ref<Expr>& index)
{
	const UpdateNode	*cur_node, *prev_node;

	cur_node = head;
	while (cur_node != NULL) {
		if (cur_node->index == index)
			break;
		cur_node = cur_node->next;
	}

	/* couldn't find index to remove */
	if (cur_node == NULL)
		return;

	prev_node = cur_node;
	cur_node = cur_node->next;
	while (cur_node != NULL) {
		std::stack<const UpdateNode*>	backward_to_head;

		if (cur_node->index != index) {
			prev_node = cur_node;
			cur_node = cur_node->next;
			continue;
		}

		const_cast<UpdateNode*>(prev_node)->next = cur_node->next;

		cur_node->refCount--;
		if (cur_node->refCount == 0)
			delete cur_node;

		/* update sequence lengths to reflect shortened tail */
		/* using a stack probably isn't the smartest way to do this
		 * but I don't think it'll be a performance bottleneck--
		 * this operation is already pretty heavy. */
		cur_node = head;
		while (cur_node != prev_node->next) {
			const_cast<UpdateNode*>(cur_node)->size--;
			backward_to_head.push(cur_node);
			cur_node = cur_node->next;
		}

		/* and compute the hashes again, starting from the tail
		 * of the first part and working up to the head.
		 * (otherwise we'll be computing the hash based on
		 *  outdated values!) */
		while (!backward_to_head.empty()) {
			cur_node = backward_to_head.top();
			const_cast<UpdateNode*>(cur_node)->computeHash();
			backward_to_head.pop();
		}

		cur_node = prev_node->next;
	}

}

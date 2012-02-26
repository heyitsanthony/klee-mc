#ifndef TRIE_H
#define TRIE_H

#include "static/Sugar.h"
#include <assert.h>
#include <list>
#include <vector>
#include <map>

template<class K, class V>
class Trie
{
public:
	typedef std::map<K, Trie<K,V>*>	nodemap_ty;
	typedef std::map<K, V>		valmap_ty;

	Trie() : children(0), leafs(0), td(0) {}

	virtual ~Trie()
	{
		if (children != NULL) {
			foreach_T (
				nodemap_ty::iterator,
				it,
				children->begin(),
				children->end())
			{
				delete it->second;
			}
			delete children;
		}

		if (leafs != NULL) delete leafs;
		if (td != NULL) delete td;
	}

	bool add(const std::vector<K>& k, V v)
	{
		std::list<TailData*>	td_list;
		Trie			*cur_trie;

		assert (k.size() > 0);

		cur_trie = this;
		/* handle internal trie nodes (n-1) */
		for (unsigned i = 0; i < k.size()-1; i++) {
			typename nodemap_ty::const_iterator	it;

			if (	cur_trie->children == NULL &&
				cur_trie->leafs == NULL &&
				cur_trie->td == NULL)
			{
				/* special case: make this a tail node
				 * to save space */
				TailData	*new_td = new TailData();
				new_td->full_key = k;
				new_td->v = v;
				new_td->depth = i;
				cur_trie->td = new_td;
				cur_trie = NULL;
				break;
			}

			/* has leafs but no children, force a child node */
			if (cur_trie->children == NULL) {
				cur_trie->children = new nodemap_ty();

				/* tail can't be set any more, queue for re-add
				* if any */
				if (cur_trie->td != NULL) {
					td_list.push_back(cur_trie->td);
					cur_trie->td = NULL;
				}
			}

			it = cur_trie->children->find(k[i]);
			if (it == cur_trie->children->end()) {
				Trie	*new_trie;
				new_trie = new Trie();
				cur_trie->children->insert(
					std::make_pair(k[i], new_trie));
				cur_trie = new_trie;
				continue;
			}

			cur_trie = it->second;
		}

		/* last node-- handle leaf trie (n-1) + 1 = n */
		if (cur_trie != NULL) {
			if (cur_trie->leafs == NULL) {
				cur_trie->leafs = new valmap_ty();
			}

			cur_trie->leafs->insert(
				std::make_pair(k[k.size()-1], v));
		}


		/* finally, restore nuked tails */
		foreach (it, td_list.begin(), td_list.end()) {
			TailData	*nuked_td = *it;
			add(nuked_td->full_key, nuked_td->v);
			delete nuked_td;
		}

		return false;
	}

	friend class const_iterator;
class const_iterator
{
public:
	const_iterator(const Trie* _t = 0)
	: t(_t), depth(0), found(false) {}
	virtual ~const_iterator(void) {}
	bool operator ==(const const_iterator& it) const
	{ return t == it.t; }
	bool operator !=(const const_iterator& it) const
	{ return t != it.t; }

	bool tryNextMin(K k, K& found_k)
	{
		assert (t);

		if (t->children != NULL) {
			typename nodemap_ty::const_iterator it;

			it = t->children->lower_bound(k);
			if (it != t->children->end()) {
				t = it->second;
				found_k = it->first;
				depth++;
				return true;
			}
		}

		if (t->leafs != NULL) {
			typename valmap_ty::const_iterator it;

			it = t->leafs->lower_bound(k);
			if (it != t->leafs->end()) {
				t = NULL;
				found_k = it->first;
				found = true;
				final_v = it->second;
				depth++;
				return true;
			}
		}

		if (t->td == NULL)
			return false;

		assert (depth >= t->td->depth);
		if (t->td->full_key[depth] < k)
			return false;

		found_k = t->td->full_key[depth];
		depth++;
		if (depth == t->td->full_key.size()) {
			found = true;
			final_v = t->td->v;
			t = NULL;
		}

		return true;
	}

	void next(K k)
	{
		assert (t);
		if (t->children != NULL) {
			typename nodemap_ty::const_iterator it;

			it = t->children->find(k);
			if (it != t->children->end()) {
				t = it->second;
				depth++;
				return;
			}
		}

		if (t->leafs != NULL) {
			typename valmap_ty::const_iterator it;
			it = t->leafs->find(k);
			if (it != t->leafs->end()) {
				t = NULL;
				found = true;
				final_v = it->second;
				depth++;
				return;
			}
		}

		if (t->td == NULL) {
			t = NULL;
			return;
		}

		assert (depth >= t->td->depth);
		if (t->td->full_key[depth] != k) {
			t = NULL;
			return;
		}

		depth++;
		if (depth == t->td->full_key.size()) {
			found = true;
			final_v = t->td->v;
			t = NULL;
			return;
		}

		/* keep on processing tail */
	}

	V get(void) const { assert (found); return final_v; }

	const_iterator& operator =(const const_iterator& in)
	{
		t = in.t;
		depth = in.depth;
		found = in.found;
		final_v = in.final_v;
		return *this;
	}

	bool isFound(void) const { return found; }

private:
	const Trie	*t;
	unsigned	depth;
	bool		found;
	V		final_v;
};

	const_iterator begin(void) const { return const_iterator(this); }
	const_iterator end(void) const { return const_iterator(NULL); }

private:
	bool set(K k, V v)
	{
		(*leafs)[k] = v;
		return true;
	}


	nodemap_ty		*children;
	valmap_ty		*leafs;

	struct TailData {
		unsigned int	depth;
		std::vector<K>	full_key;
		V		v;
	};

	TailData		*td;

};

#endif
#ifndef TRIE_H
#define TRIE_H

#include <iostream>
#include "static/Sugar.h"
#include <assert.h>
#include <list>
#include <vector>
#include <map>

template<class K, class V>
class Trie
{
private:
	struct TailData {
		unsigned int	depth;
		std::vector<K>	full_key;
		V		v;
	};
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

	void dump(std::ostream& os) const
	{
		os << "[Trie] Dumping ptr=" << (void*)this << '\n';
		if (td != NULL) {
			os << "[Trie] Tail:\n";
			os << "Key: ";
			foreach (it, td->full_key.begin(), td->full_key.end())
				os << (void*)(*it) << ' ';
			os << '\n';
		}

		if (children != NULL) {
			os << "[Trie] children:\n";
			foreach (it, children->begin(), children->end()) {
				os << (void*)it->first << '\n';
			}
		}

		if (leafs != NULL) {
			os << "[Trie] leafs:\n";
			foreach (it, leafs->begin(), leafs->end()) {
				os << (void*)it->first << '\n';
			}
		}
		os << "[Trie] Dump done.\n";
	}


	bool add(const std::vector<K>& k, V v)
	{
		Trie			*cur_trie;
		std::list<TailData*>	td_list;

		assert (k.size() > 0);

		/* handle internal trie nodes (n-1) */
		cur_trie = handleInternal(k, v, td_list);

		/* last node-- handle leaf trie (n-1) + 1 = n */
		/* if key is a Tail, then cur_trie is null */
		if (cur_trie != NULL) {
			if (cur_trie->leafs == NULL)
				cur_trie->leafs = new valmap_ty();

			if (cur_trie->td != NULL) {
				td_list.push_back(cur_trie->td);
				cur_trie->td = NULL;
			}

			cur_trie->leafs->insert(std::make_pair(k.back(), v));
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
	: t(_t), t_last(0), depth(0), found(false) {}
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
				t_last = t;
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
			t_last = t;
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
				t_last = t;
				t = NULL;
				found = true;
				final_v = it->second;
				depth++;
				return;
			}
		}

		if (t->td == NULL) {
			t_last = t;
			t = NULL;
			return;
		}

		assert (depth >= t->td->depth);
		if (t->td->full_key[depth] != k) {
			t_last = t;
			t = NULL;
			return;
		}

		depth++;
		if (depth == t->td->full_key.size()) {
			found = true;
			final_v = t->td->v;
			t_last = t;
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
	void dump(std::ostream& os) const
	{
		os << "[TrieIt] depth: " << depth << '\n';

		if (found) {
			os << "[TrieIt] final_v: "
				<< (void*)final_v << '\n';
		}

		if (t != NULL) {
			os << "[TrieIt] 't':\n";
			t->dump(os);
		}

		if (t_last != NULL) {
			os << "[TrieIt] 't_last':\n";
			t_last->dump(os);
		}
	}
private:
	const Trie	*t;
	const Trie	*t_last;
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

	Trie* handleInternal(
		const std::vector<K>& k,
		V v,
		std::list<TailData*>& td_list)
	{
		Trie	*cur_trie;

		cur_trie = this;

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
				return NULL;
			}

			/* has leafs but no children, force a child node */
			if (cur_trie->children == NULL) {
				cur_trie->children = new nodemap_ty();

				/* tail can't be set any more;
				 * queue for re-add */
				if (cur_trie->td != NULL) {
					td_list.push_back(cur_trie->td);
					cur_trie->td = NULL;
				}
			}

			/* add child */
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

		return cur_trie;
	}

	nodemap_ty		*children;
	valmap_ty		*leafs;	/* values one key elem from a match */
	TailData		*td;	/* don't waste space on tails */
};

#endif
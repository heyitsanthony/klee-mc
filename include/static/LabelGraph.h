#ifndef _STATIC_UTIL_LABEL_GRAPH_H
#define	_STATIC_UTIL_LABEL_GRAPH_H

#include "static/Sugar.h"

#include <assert.h>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <vector>

template <typename V, typename L>
class LabelGraphNode {
public:

    LabelGraphNode() : value(0) {

    }

    V value;
    std::map<LabelGraphNode<V,L>*,L> succs;
    std::map<LabelGraphNode<V,L>*,L> preds;
};

template <typename V, typename L>
class LabelGraph {
public:
    typedef LabelGraphNode<V,L> node_ty;

    bool hasNode(V v) {
        return getNode(v);
    }

    LabelGraphNode<V,L>* getNode(V v) {
        typename std::map<V, LabelGraphNode<V,L> >::iterator it = v2node.find(v);

        return (it == v2node.end()) ? NULL : &it->second;
    }

    LabelGraphNode<V,L>* addNode(V v) {
        assert(v);

        std::pair<typename std::map<V, LabelGraphNode<V,L> >::iterator, bool> res;
        res = v2node.insert(std::make_pair(v, LabelGraphNode<V,L > ()));
        LabelGraphNode<V,L>* node = &((res.first)->second);
        assert(node);

        if (res.second) {
            node->value = v;
            nodes.push_back(node);
        }

        return node;
    }

    void addEdge(V v1, V v2, L l) {
        addNode(v1);
        addNode(v2);

        LabelGraphNode<V,L>* n1 = getNode(v1);
        LabelGraphNode<V,L>* n2 = getNode(v2);

        n1->succs.insert(std::make_pair(n2,l));
        n2->preds.insert(std::make_pair(n1,l));
    }

    bool hasEdge(V v1, V v2) {
        LabelGraphNode<V,L>* n1 = getNode(v1);
        LabelGraphNode<V,L>* n2 = getNode(v2);

        return n1->succs.find(n2) != n1->succs.end();
    }

    L getLabel(V v1, V v2) {
        LabelGraphNode<V,L>* n1 = getNode(v1);
        LabelGraphNode<V,L>* n2 = getNode(v2);
        assert(n1 && n2);
        assert(n1->succs.find(n2) != n1->succs.end());
        return n1->succs[n2];
    }

    void setLabel(V v1, V v2, L l) {
        LabelGraphNode<V,L>* n1 = getNode(v1);
        LabelGraphNode<V,L>* n2 = getNode(v2);
        assert(n1 && n2);
        assert(n1->succs.find(n2) != n1->succs.end());
        n1->succs[n2] = l;
    }

    LabelGraph() {

    }

    std::map<V, LabelGraphNode<V,L > > v2node;
    std::list<LabelGraphNode<V,L>*> nodes;
};

#endif


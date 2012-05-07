#ifndef _STATIC_UTIL_GRAPH_H
#define	_STATIC_UTIL_GRAPH_H

#include "static/Sugar.h"

#include <assert.h>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <vector>

template <typename V>
class GenericGraph;

template <typename V>
class GenericGraphNode {
public:

    GenericGraphNode() : value(0) {

    }

    GenericGraphNode<V>* next() {
        return *(succs.begin());
    }

    V value;
    std::set<GenericGraphNode<V>*> succs;
    std::set<GenericGraphNode<V>*> preds;
};

template <typename V>
class GenericGraph {
public:

    bool hasNode(V v) { return getNode(v); }

    GenericGraphNode<V>* getNode(V v) {
        typename std::map<V, GenericGraphNode<V> >::iterator it = v2node.find(v);

        return (it == v2node.end()) ? NULL : &it->second;
    }

    GenericGraphNode<V>* addNode(V v) {
        assert(v);

        std::pair<typename std::map<V, GenericGraphNode<V> >::iterator, bool> res;
        res = v2node.insert(std::make_pair(v, GenericGraphNode<V > ()));
        GenericGraphNode<V>* node = &((res.first)->second);
        assert(node);

        if (res.second) {
            node->value = v;
            nodes.push_back(node);
        }

        return node;
    }

    void addEdge(V v1, V v2) {
        addNode(v1);
        addNode(v2);

        GenericGraphNode<V>* n1 = getNode(v1);
        GenericGraphNode<V>* n2 = getNode(v2);

        n1->succs.insert(n2);
        n2->preds.insert(n1);
    }

    bool hasEdge(V v1, V v2) {
        GenericGraphNode<V>* n1 = getNode(v1);
        GenericGraphNode<V>* n2 = getNode(v2);

        return n1->succs.find(n2) != n1->succs.end();
    }

    //removes all incident edges

    void removeNode(V v1) {
        GenericGraphNode<V>* v1node = get(v1);

        foreach(it, v1node->succs.begin(), v1node->succs.end()) {
            GenericGraphNode<V>* v1nodesucc = *it;
            v1nodesucc->preds.erase(v1node);
        }

        foreach(it, v1node->preds.begin(), v1node->preds.end()) {
            GenericGraphNode<V>* v1nodepred = *it;
            v1nodepred->succs.erase(v1node);
        }

        foreach(it, nodes.begin(), nodes.end()) {
            if (*it == v1node) {
                nodes.erase(it);
                break;
            }
        }

        v2node.erase(v1);
    }

    GenericGraph() {}

    unsigned getNumNodes(void) const { return v2node.size(); }

    std::map<V, GenericGraphNode<V > > v2node;
    std::list<GenericGraphNode<V>*> nodes;
};

#endif


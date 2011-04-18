#define BOOST_NO_HASH

#ifndef _JOHNSONALLPAIRSSHORTESTPATH_H
#define	_JOHNSONALLPAIRSSHORTESTPATH_H

#include <boost/config.hpp>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <vector>
#include <boost/property_map.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graphviz.hpp>
#include <boost/graph/johnson_all_pairs_shortest.hpp>

#include "static/LabelGraph.h"

using namespace boost;

template <typename V>
class JohnsonAllPairsShortestPath {
public:
    typedef adjacency_list < vecS, vecS, directedS > Graph;
    typedef graph_traits < Graph >::vertex_descriptor Vertex;
    typedef graph_traits < Graph >::edge_descriptor Edge;

    Graph boostgraph;
    LabelGraph<V, int64_t>* graph;
    std::map<LabelGraphNode<V, int64_t>*, Vertex> node2vertex;
    std::map<Vertex, int> verteximap;
    std::vector<std::vector<int64_t> > D;

    struct cmp_edge :
    public std::binary_function<Edge, Edge, bool> {

        bool operator()(const Edge &e1, const Edge & e2) const {
            return e1.get_property() < e2.get_property();
        }
    };

    JohnsonAllPairsShortestPath(LabelGraph<V, int64_t>* g) : graph(g) {
        std::map<Edge, int64_t, cmp_edge> weightmap;

        int count = 0;

        foreach(it, graph->nodes.begin(), graph->nodes.end()) {
            LabelGraphNode<V, int64_t>* node = *it;
            Vertex u = add_vertex(boostgraph);
            node2vertex.insert(std::make_pair(node, u));
            verteximap[u] = count;
            count++;
        }

        foreach(it, graph->nodes.begin(), graph->nodes.end()) {
            LabelGraphNode<V, int64_t>* node = *it;
            Vertex u = node2vertex[node];

            foreach(succit, node->succs.begin(), node->succs.end()) {
                LabelGraphNode<V, int64_t>* node2 = succit->first;
                int64_t w = succit->second;
                Vertex v = node2vertex[node2];

                Edge e;
                bool inserted;
                tie(e, inserted) = add_edge(u, v, boostgraph);
                assert(inserted);
                assert(w > 0);
                weightmap[e] = w;
            }
        }

        D.resize(count);

        foreach(it, D.begin(), D.end()) {
            it->resize(count);
        }

        boost::associative_property_map< std::map<Edge, int64_t, cmp_edge> > weightpmap(weightmap);
        boost::associative_property_map< std::map<Vertex, int> > vertexipmap(verteximap);

        johnson_all_pairs_shortest_paths(boostgraph, D, vertexipmap, weightpmap, 0);
    }

    int64_t dist(V val1, V val2) {
        Vertex v1 = node2vertex[graph->getNode(val1)];
        Vertex v2 = node2vertex[graph->getNode(val2)];
        int vi1 = verteximap[v1];
        int vi2 = verteximap[v2];

        return D[vi1][vi2];
    }

};


#endif


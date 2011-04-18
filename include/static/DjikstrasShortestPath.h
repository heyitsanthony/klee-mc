#define BOOST_NO_HASH

#ifndef _DJIKSTRASSHORTESTPATH_H
#define	_DJIKSTRASSHORTESTPATH_H

#include <boost/config.hpp>
#include <iostream>
#include <fstream>

#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/dijkstra_shortest_paths.hpp>

#include "static/LabelGraph.h"

using namespace boost;

template <typename V>
class DjikstrasShortestPath {
public:
    typedef adjacency_list < vecS, vecS, directedS > Graph;
    typedef graph_traits < Graph >::vertex_descriptor Vertex;
    typedef graph_traits < Graph >::edge_descriptor Edge;

    Graph boostgraph;
    std::map<LabelGraphNode<V, int64_t>*, Vertex> node2vertex;
    std::map<V, int64_t> distmap;

    struct cmp_edge : public std::binary_function<Edge, Edge, bool> {

        bool operator()(const Edge &e1, const Edge & e2) const {
            return e1.get_property() < e2.get_property();
        }
    };

    DjikstrasShortestPath(LabelGraph<V, int64_t>* graph, V v) {
        std::map<Edge, int64_t, cmp_edge> weightmap;
        std::map<Vertex, int> verteximap;

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

        std::map<Vertex, Vertex> p;
        std::map<Vertex, int64_t> d;
        Vertex s = node2vertex[graph->getNode(v)];

        boost::associative_property_map< std::map<Vertex, Vertex> > predpmap(p);
        boost::associative_property_map< std::map<Vertex, int64_t> > distpmap(d);
        boost::associative_property_map< std::map<Edge, int64_t, cmp_edge> > weightpmap(weightmap);
        boost::associative_property_map< std::map<Vertex, int> > vertexipmap(verteximap);

        dijkstra_shortest_paths(boostgraph, s, predpmap, distpmap, weightpmap, vertexipmap,
                std::less<int64_t > (), closed_plus<int64_t > (),
                (std::numeric_limits<int64_t>::max)(), 0,
                default_dijkstra_visitor());

        foreach(it, node2vertex.begin(), node2vertex.end()) {
            LabelGraphNode<V, int64_t>* n = it->first;
            Vertex v = it->second;
            distmap[n->value] = d[v];
        }
    }

    int64_t dist(V v) {
        return distmap[v];
    }

};


#endif


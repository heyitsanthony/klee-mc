#ifndef _STATIC_UTIL_GRAPH_H
#define	_STATIC_UTIL_GRAPH_H

#include "Sugar.h"

#include <assert.h>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <vector>
#include <fstream>

namespace klee {

    class SegmentNode {
    public:

        SegmentNode();

        unsigned color;
        unsigned value;
        std::map<SegmentNode*, unsigned> succs;
        std::map<SegmentNode*, unsigned> preds;
    };

    class SegmentGraph {
    public:

        enum Color {
            WHITE, GRAY, BLACK
        };

        unsigned nodeCount();
        void writeDOTGraph();
        bool hasNode(unsigned v);
        SegmentNode* get(unsigned v);
        void addEdge(unsigned v1, unsigned v2);
        bool hasEdge(unsigned v1, unsigned v2);
        void removeEdge(unsigned a, unsigned b);
        bool checkCycleIfAdd(unsigned a, unsigned b);
        void dfs(SegmentNode* start, bool& hasCycle);
        void dfs_visit(SegmentNode * u, bool& hasCycle);
        SegmentGraph();

        std::map<unsigned, SegmentNode > v2node;
    };
}

#endif

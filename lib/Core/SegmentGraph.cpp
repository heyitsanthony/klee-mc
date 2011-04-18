#include "SegmentGraph.h"

using namespace klee;

SegmentNode::SegmentNode() : color(0), value(0) {

}

unsigned SegmentGraph::nodeCount() {
  return v2node.size();
}

void SegmentGraph::writeDOTGraph() {
  std::ofstream os;
  std::string name = "segment.dot";
  os.open(name.c_str());

  os << "digraph {\n";

  foreach(itn, v2node.begin(), v2node.end()) {
    SegmentNode* n = &itn->second;

    foreach(itsucc, n->succs.begin(), n->succs.end()) {
      if (!itsucc->second) continue;
      SegmentNode* s = itsucc->first;

      os << "\t" << n->value << " -> " << s->value << "[label = " << itsucc->second << "] ;\n";
    }
  }

  os << "}";

  os.flush();
  os.close();
}

SegmentNode* SegmentGraph::get(unsigned v) {
  SegmentNode* n = &v2node[v];
  n->value = v;
  return n;
}

void SegmentGraph::addEdge(unsigned v1, unsigned v2) {
  SegmentNode* n1 = get(v1);
  SegmentNode* n2 = get(v2);

  n1->succs[n2]++;
  n2->preds[n1]++;

  assert(n1->succs[n2]);
  assert(n2->preds[n1]);
}

bool SegmentGraph::hasEdge(unsigned v1, unsigned v2) {
  SegmentNode* n1 = get(v1);
  SegmentNode* n2 = get(v2);

  return n1->succs[n2];
}

void SegmentGraph::removeEdge(unsigned a, unsigned b) {
  SegmentNode* n1 = get(a);
  SegmentNode* n2 = get(b);

  assert(n1->succs[n2]);
  assert(n2->preds[n1]);

  n1->succs[n2]--;
  n2->preds[n1]--;
}

bool SegmentGraph::checkCycleIfAdd(unsigned a, unsigned b) {
  if (hasEdge(a, b)) {
    return false;
  }

  SegmentNode* n1 = get(a);
  addEdge(a, b);
  bool hasCycle = false;
  dfs(n1, hasCycle);
  removeEdge(a, b);
  return hasCycle;
}

/*
 * DFS(G)
for each vertex u in unsigned
color[u] := WHITE
p[u] = u
end for
time := 0
if there is a starting vertex s
call DFS-VISIT(G, s)
for each vertex u in unsigned
if color[u] = WHITE
  call DFS-VISIT(G, u)
end for
return (p,d_time,f_time)*/

void SegmentGraph::dfs(SegmentNode* start, bool& hasCycle) {

  foreach(it, v2node.begin(), v2node.end()) {
    SegmentNode* u = &it->second;
    u->color = WHITE;
  }

   dfs_visit(start, hasCycle);
  if (hasCycle) return;

  foreach(it, v2node.begin(), v2node.end()) {
    SegmentNode* u = &it->second;
    if (u->color == WHITE) {
      dfs_visit(u, hasCycle);
      if (hasCycle) return;
    }
  }
}

/*
DFS-VISIT(G, u)
color[u] := GRAY
d_time[u] := time := time + 1
for each v in Adj[u]
if (color[v] = WHITE)
  p[v] = u
  call DFS-VISIT(G, v)
else if (color[v] = GRAY)
  ...
else if (color[v] = BLACK)
  ...
end for
color[u] := BLACK
f_time[u] := time := time + 1*/

void SegmentGraph::dfs_visit(SegmentNode* u, bool& hasCycle) {
  u->color = GRAY;

  foreach(it, u->succs.begin(), u->succs.end()) {
    if (!it->second) continue;
    
    SegmentNode* v = it->first;
    if (v->color == WHITE) {
      dfs_visit(v, hasCycle);
      if (hasCycle) return;
    } else if (v->color == GRAY) {
      hasCycle = true;
      return;
    }
  }
  u->color = BLACK;
}

SegmentGraph::SegmentGraph() {

}

#include "ControlDependence.h"

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/reverse_graph.hpp>
#include <boost/graph/dominator_tree.hpp>
#include <iostream>
#include <boost/graph/strong_components.hpp>

#include <limits>

#include "llvm/Support/Casting.h"
#include "llvm/Value.h"
#include "llvm/Instructions.h"
#include "llvm/Instruction.h"
#include "static/CallGraph.h"
#include "StaticRecord.h"
#include "klee/Internal/Module/KModule.h"
#include "llvm/Function.h"
#include "static/AliasingRunner.h"
#include "static/Support.h"

#include "llvm/Support/CommandLine.h"
#include <map>
#include <set>
#include <list>
#include <fstream>
#include <vector>

using namespace llvm;
using namespace boost;
using namespace klee;


cl::opt<bool>
WriteControlGraphs("write-control-graphs",
        cl::init(false));

CDNode::CDNode() : idom(0) {

}

std::string CDNode::name() {
  return "cdnode";
}

void CDNode::print() {
  std::cout << "cdnode";
}

bool CDNode::isPlaceHolder() {
  return false;
}

bool CDNode::isACFGNode() {
  return false;
}

bool CDNode::isACDGNode() {
  return false;
}

void CDNode::addICFGSucc(CDNode* n) {
  icfg_succs.insert(n);
  n->icfg_preds.insert(this);
}

void CDNode::addPCGSucc(CDNode* n) {
  pcg_succs.insert(n);
  n->pcg_preds.insert(this);
}

void CDNode::addACFGSucc(CDNode* n) {
  acfg_succs.insert(n);
  n->acfg_preds.insert(this);
}

void CDNode::addACDGSucc(CDNode* n) {
  acdg_succs.insert(n);
  n->acdg_preds.insert(this);
}

CallInst* CDNode::toCallInst() {
  return NULL;
}

ReturnInst* CDNode::toReturnInst() {
  return NULL;
}

EntryCDNode* CDNode::toEntry() {
  return NULL;
}

ExitCDNode* CDNode::toExit() {
  return NULL;
}

SuperExitCDNode* CDNode::toSuperExit() {
  return NULL;
}

ReturnCDNode* CDNode::toReturn() {
  return NULL;
}

StartCDNode* CDNode::toStart() {
  return NULL;
}

StaticRecordCDNode* CDNode::toStaticRecord() {
  return NULL;
}

ReturnPredCDNode* CDNode::toReturnPred() {
  return NULL;
}

//////////////////////////////////////////////////////////////////////////////

ExitCDNode::ExitCDNode(Function* _function) : function(_function) {
}

std::string ExitCDNode::name() {
  return "exit";
}

ExitCDNode* ExitCDNode::toExit() {
  return this;
}

void ExitCDNode::print() {
  std::cout << "exit: " << function->getNameStr();
}

bool ExitCDNode::isACFGNode() {
  return true;
}

bool ExitCDNode::isACDGNode() {
  return true;
}

//////////////////////////////////////////////////////////////////////////////

EntryCDNode::EntryCDNode(Function* _function) : function(_function) {
}

void EntryCDNode::print() {
  std::cout << "entry";
}

std::string EntryCDNode::name() {
  return "entry";
}

EntryCDNode* EntryCDNode::toEntry() {
  return this;
}

bool EntryCDNode::isPlaceHolder() {
  return true;
}

bool EntryCDNode::isACFGNode() {
  return true;
}

bool EntryCDNode::isACDGNode() {
  return true;
}

//////////////////////////////////////////////////////////////////////////////

void SuperExitCDNode::print() {
  std::cout << "superexit";
}

std::string SuperExitCDNode::name() {
  return "superexit";
}

SuperExitCDNode* SuperExitCDNode::toSuperExit() {
  return this;
}

bool SuperExitCDNode::isACFGNode() {
  return true;
}

bool SuperExitCDNode::isACDGNode() {
  return false;
}

//////////////////////////////////////////////////////////////////////////////

void ReturnCDNode::print() {
  std::cout << "return";
}

std::string ReturnCDNode::name() {
  std::string s = "return_";
  if (Function * f = callinst->getCalledFunction()) {
    s += f->getNameStr();
  }
  s += Support::str((unsigned long) callinst);
  return s;
}

ReturnCDNode::ReturnCDNode(CallInst* ci) : callinst(ci) {
}

ReturnCDNode* ReturnCDNode::toReturn() {
  return this;
}

bool ReturnCDNode::isPlaceHolder() {
  return true;
}

bool ReturnCDNode::isACFGNode() {
  return true;
}

bool ReturnCDNode::isACDGNode() {
  return true;
}

//////////////////////////////////////////////////////////////////////////////

void ReturnPredCDNode::print() {
  std::cout << "retpred";
}

std::string ReturnPredCDNode::name() {
  std::string s = "retpred_";
  if (Function * f = callinst->getCalledFunction()) {
    s += f->getNameStr();
  }
  s += Support::str((unsigned long) callinst);
  return s;
}

ReturnPredCDNode::ReturnPredCDNode(CallInst* ci) : callinst(ci) {

}

ReturnPredCDNode* ReturnPredCDNode::toReturnPred() {
  return this;
}

bool ReturnPredCDNode::isACFGNode() {
  return true;
}

bool ReturnPredCDNode::isACDGNode() {
  return false;
}

//////////////////////////////////////////////////////////////////////////////

void StartCDNode::print() {
  std::cout << "start";
}

std::string StartCDNode::name() {
  std::string s = "start";
  return s;
}

StartCDNode* StartCDNode::toStart() {
  return this;
}

bool StartCDNode::isACFGNode() {
  return false;
}

bool StartCDNode::isACDGNode() {
  return false;
}

//////////////////////////////////////////////////////////////////////////////

std::string StaticRecordCDNode::name() {
  std::string s = "rec_" + staticRecord->basicBlock->getNameStr() + "_" + Support::str(staticRecord->index);
  return s;
}

void StaticRecordCDNode::print() {
  std::cout << "static: ";
  std::cout << "fn=" << staticRecord->function->getNameStr() << " ";
  std::cout << "bb=" << staticRecord->basicBlock->getNameStr() << " ";
  std::cout << "i=" << Support::str(staticRecord->index) << " ";
  std::cout << *(staticRecord->insts.front()) << std::endl;
}

StaticRecordCDNode::StaticRecordCDNode(StaticRecord* _staticRecord) : staticRecord(_staticRecord) {

}

bool StaticRecordCDNode::isACFGNode() {
  return true;
}

bool StaticRecordCDNode::isACDGNode() {
  return true;
}

StaticRecordCDNode* StaticRecordCDNode::toStaticRecord() {
  return this;
}

bool StaticRecordCDNode::isPredicate() {

  if (BranchInst * bi = dyn_cast<BranchInst > (staticRecord->insts.back())) {
    return bi->isConditional();
  }

  return isa<SwitchInst > (staticRecord->insts.back());
}

CallInst* StaticRecordCDNode::toCallInst() {
  return dyn_cast<CallInst > (staticRecord->insts.back());
}

ReturnInst* StaticRecordCDNode::toReturnInst() {
  return dyn_cast<ReturnInst > (staticRecord->insts.back());
}

//////////////////////////////////////////////////////////////////////////////

ACFG::ACFG(llvm::Function* _function, StaticRecordManager* _recm, CallGraph* _callgraph) : function(_function), recm(_recm), callgraph(_callgraph) {
  buildACFG();
  buildBGLACFG();
  runPostDom();
  constructControlEdges();
}

void ACFG::san(std::string& s) {
  std::replace(s.begin(), s.end(), '.', '_');
  std::replace(s.begin(), s.end(), '-', '_');
}

void ACFG::writeACDGGraph() {
  std::ofstream os;
  std::string name = function->getNameStr() + ".acdg.dot";
  os.open(name.c_str());

  os << "digraph {\n";

  foreach(itn, nodes.begin(), nodes.end()) {
    CDNode* n = *itn;

    foreach(itsucc, n->acdg_succs.begin(), n->acdg_succs.end()) {
      CDNode* s = *itsucc;

      std::string s1 = n->name();
      std::string s2 = s->name();
      san(s1);
      san(s2);
      os << "\t" << s1 << " -> " << s2 << ";\n";
    }
  }

  os << "}";

  os.flush();
  os.close();
}

void ACFG::writeACFGGraph() {
  std::ofstream os;
  std::string name = function->getNameStr() + ".acfg.dot";
  os.open(name.c_str());

  os << "digraph {\n";

  foreach(itn, nodes.begin(), nodes.end()) {
    CDNode* n = *itn;

    foreach(itsucc, n->acfg_succs.begin(), n->acfg_succs.end()) {
      CDNode* s = *itsucc;

      std::string s1 = n->name();
      std::string s2 = s->name();
      san(s1);
      san(s2);
      os << "\t" << s1 << " -> " << s2 << ";\n";
    }
  }

  os << "}";

  os.flush();
  os.close();
}

ReturnCDNode* ACFG::getReturnNode(CallInst* ci) {
  assert(retnodes.find(ci) != retnodes.end());
  return retnodes[ci];
}

void ACFG::addSuccsOfTo(StaticRecord* of, CDNode* to) {

  foreach(succit, of->succs.begin(), of->succs.end()) {
    StaticRecord* succrec = *succit;
    if (succrec->isPHI()) {
      StaticRecordCDNode* succrecn = phinodes[StaticRecord::hash(of, succrec)];
      if (!succrecn) {
        std::cout << " f=" << function->getNameStr() << std::endl;
        std::cout << " of=" << of->name() << std::endl;
        std::cout << " sr=" << succrec->name() << std::endl;
        writeACFGGraph();
        assert(false);
      }
      assert(succrecn);
      assert(succrec->succs.size() == 1);
      StaticRecord* succrec2 = *(succrec->succs.begin());
      assert(!succrec2->isPHI());

      StaticRecordCDNode* succrecn2 = regnodes[succrec2];
      assert(succrecn2);

      to->addACFGSucc(succrecn);
      succrecn->addACFGSucc(succrecn2);
    } else {
      StaticRecordCDNode* succrecn = regnodes[succrec];
      assert(succrecn);
      to->addACFGSucc(succrecn);
    }
  }
}

void ACFG::buildACFG() {

  entry = new EntryCDNode(function);
  exit = new ExitCDNode(function);
  start = new StartCDNode();
  superExit = new SuperExitCDNode();

  nodes.insert(entry);
  nodes.insert(exit);
  nodes.insert(start);
  nodes.insert(superExit);

  std::vector<StaticRecord*>& vc = recm->funrecs.find(function)->second;
  std::vector<StaticRecord*>::iterator it = vc.begin();
  std::vector<StaticRecord*>::iterator eit = vc.end();

  for (; it != eit; ++it) {
    StaticRecord* sr = *it;
    if (sr->isPHI()) {

      foreach(phipredit, sr->preds.begin(), sr->preds.end()) {
        StaticRecord* srpred = *phipredit;
        StaticRecordCDNode* srn = new StaticRecordCDNode(sr);
        unsigned h = StaticRecord::hash(srpred, sr);

        assert(phinodes.find(h) == phinodes.end());

        phinodes[h] = srn;
        nodes.insert(srn);
        srns.push_back(srn);
      }
    } else {
      StaticRecordCDNode* srn = new StaticRecordCDNode(sr);
      regnodes[sr] = srn;
      nodes.insert(srn);
      srns.push_back(srn);
    }
  }

  StaticRecord* entryRec = recm->entry[function];
  assert(entryRec);
  start->addACFGSucc(entry);
  start->addACFGSucc(superExit);
  exit->addACFGSucc(superExit);
  assert(regnodes.count(entryRec));
  entry->addACFGSucc(regnodes[entryRec]);

  foreach(it, srns.begin(), srns.end()) {
    StaticRecordCDNode* recn = *it;
    StaticRecord* rec = recn->staticRecord;

    if (CallInst * ci = recn->toCallInst()) {
      if (callgraph->isBaseMustHalt(ci)) {
        rec->controlsExit = true;
        recn->addACFGSucc(superExit);
      } else {
        ReturnCDNode* retn = new ReturnCDNode(ci);
        nodes.insert(retn);
        assert(!retnodes.count(ci));
        retnodes[ci] = retn;

        if (callgraph->mayHalt(ci)) {
          ReturnPredCDNode* retpredn = new ReturnPredCDNode(ci);
          nodes.insert(retpredn);
          assert(!retprednodes.count(ci));
          retprednodes[ci] = retpredn;
          recn->addACFGSucc(retpredn);
          retpredn->addACFGSucc(retn);
          retpredn->addACFGSucc(superExit);
        } else {
          recn->addACFGSucc(retn);
        }

        addSuccsOfTo(rec, retn);
      }
    } else {
      addSuccsOfTo(rec, recn);
    }

    if (recn->toReturnInst()) {
      recn->addACFGSucc(exit);
    }

    if (isa<UnreachableInst > (recn->staticRecord->insts.back())) {
      //recn->addACFGSucc(superExit);
      recn->addACFGSucc(exit);
    }
  }

}

void ACFG::buildBGLACFG() {


}

void ACFG::runPostDom() {

  typedef adjacency_list < listS, listS, bidirectionalS, property<vertex_index_t, std::size_t>, no_property > Graph;
  typedef graph_traits < Graph >::vertex_descriptor Vertex;
  typedef property_map<Graph, vertex_index_t>::type IndexMap;
  typedef iterator_property_map<std::vector<Vertex>::iterator, IndexMap> PredMap;

  Graph bglacfg;
  std::map<Vertex, CDNode*> vertex2node;
  std::map<CDNode*, Vertex> node2vertex;

  foreach(it, nodes.begin(), nodes.end()) {
    CDNode* n = *it;
    Vertex u = add_vertex(bglacfg);
    node2vertex[n] = u;
    vertex2node.insert(std::make_pair(u, n));
  }

  foreach(it, nodes.begin(), nodes.end()) {
    CDNode* n = *it;

    foreach(succit, n->acfg_succs.begin(), n->acfg_succs.end()) {
      CDNode* succn = *succit;
      add_edge(node2vertex[n], node2vertex[succn], bglacfg);
    }
  }

  Vertex exitVertex = node2vertex[superExit];

  std::vector<Vertex> domTreePredVector;
  IndexMap indexMap(get(vertex_index, bglacfg));
  graph_traits<Graph>::vertex_iterator uItr, uEnd;
  int j = 0;

  for (boost::tie(uItr, uEnd) = vertices(bglacfg); uItr != uEnd; ++uItr, ++j) {
    put(indexMap, *uItr, j);
  }

  domTreePredVector = std::vector<Vertex > (num_vertices(bglacfg), graph_traits<Graph>::null_vertex());
  PredMap domTreePredMap = make_iterator_property_map(domTreePredVector.begin(), indexMap);
  lengauer_tarjan_dominator_tree(make_reverse_graph(bglacfg), exitVertex, domTreePredMap);

  for (boost::tie(uItr, uEnd) = vertices(bglacfg); uItr != uEnd; ++uItr) {
    Vertex u = *uItr;
    CDNode* unode = vertex2node[u];
    assert(unode);

    if (get(domTreePredMap, u) != graph_traits<Graph>::null_vertex()) {

      Vertex uidom = get(domTreePredMap, u);
      CDNode* uidomnode = vertex2node[uidom];
      assert(uidomnode);
      unode->idom = uidomnode;
      uidomnode->postDomChildren.insert(unode);
    }
  }

  foreach(it, nodes.begin(), nodes.end()) {
    CDNode* c = *it;
    if (!c->idom && !c->toSuperExit()) {
      std::cout << "f=" << function->getNameStr() << std::endl;
      std::cout << " NO IDOM: f=" << function->getNameStr() << std::endl;
      std::cout << c->name() << std::endl;
      assert(false);
    }
  }
}

void ACFG::constructControlEdges() {
  std::list<CDNode*> worklist;
  std::set<CDNode*> visited;
  std::list<CDNode*> topo;

  CDNode* root = superExit;
  assert(!root->idom);

  worklist.push_back(root);
  while (!worklist.empty()) {
    CDNode* w = worklist.front();
    assert(visited.find(w) == visited.end());
    visited.insert(w);
    worklist.pop_front();

    topo.push_front(w);

    foreach(it, w->postDomChildren.begin(), w->postDomChildren.end()) {
      CDNode* c = *it;
      worklist.push_back(c);
    }
  }

  std::map<CDNode*, std::set<CDNode*> > frontier;
  while (!topo.empty()) {
    CDNode* x = topo.front();
    topo.pop_front();

    foreach(it, x->acfg_preds.begin(), x->acfg_preds.end()) {
      CDNode* y = *it;
      if (y->idom != x) {
        frontier[x].insert(y);
      }
    }

    foreach(zit, x->postDomChildren.begin(), x->postDomChildren.end()) {
      CDNode* z = *zit;

      std::set<CDNode*> add;

      foreach(yit, frontier[z].begin(), frontier[z].end()) {
        CDNode* y = *yit;
        if (y->idom != x) {
          add.insert(y);
        }
      }
      frontier[x].insert(add.begin(), add.end());
    }
  }

  foreach(yit, nodes.begin(), nodes.end()) {
    CDNode* y = *yit;

    foreach(xit, frontier[y].begin(), frontier[y].end()) {
      CDNode* x = *xit;
      x->addPCGSucc(y);
    }
  }
}

void ACFG::checkRep() {
  std::list<CDNode*> worklist;
  std::set<CDNode*> visited;

  worklist.push_back(start);
  visited.insert(start);
  while (!worklist.empty()) {
    CDNode* n = worklist.front();
    worklist.pop_front();

    foreach(it, n->acfg_succs.begin(), n->acfg_succs.end()) {
      CDNode* ns = *it;
      if (!visited.count(ns)) {
        worklist.push_back(ns);
        visited.insert(ns);
      }
    }
  }

  foreach(it, nodes.begin(), nodes.end()) {
    CDNode* n = *it;

    if (!visited.count(n)) {
      std::cout << "CHECK REP FAILED" << std::endl;
      n->print();
      std::cout << std::endl;
      assert(false);
    }
  }
}

//////////////////////////////////////////////////////////////////////////

ControlDependence::ControlDependence(Module* _module, StaticRecordManager* _recm) : module(_module), recm(_recm) {
  aliasingRunner = new AliasingRunner(module);

  buildACDG();
  findControls();
  propagateControlsExit();

  const std::vector<StaticRecord*>& nodes = recm->nodes;

  typedef adjacency_list < vecS, vecS, directedS > Graph2;
  typedef graph_traits < Graph2 >::vertex_descriptor Vertex2;
  typedef graph_traits < Graph2 >::vertices_size_type vs_t;

  std::vector<StaticRecord*> vertex2node;
  std::map<StaticRecord*, vs_t> node2vertex;
  Graph2 G;

  foreach(it, nodes.begin(), nodes.end()) {
    StaticRecord* n = *it;
    Vertex2 u = add_vertex(G);
    node2vertex[n] = u;
    vertex2node.push_back(n);
    assert((u + 1) == vertex2node.size());
  }

  foreach(it, nodes.begin(), nodes.end()) {
    StaticRecord* n = *it;

    foreach(succit, n->control_succs.begin(), n->control_succs.end()) {
      StaticRecord* succn = *succit;
      add_edge(node2vertex[n], node2vertex[succn], G);
    }
  }

  std::vector<int> component(num_vertices(G)), discover_time(num_vertices(G));
  std::vector<default_color_type> color(num_vertices(G));
  std::vector<Vertex2> root(num_vertices(G));
  unsigned num = strong_components(G, &component[0],
          root_map(&root[0]).
          color_map(&color[0]).
          discover_time_map(&discover_time[0]));

  std::cout << "numvert=" << num_vertices(G) << " numcomps=" << num << std::endl;
  std::vector<StaticRecordSCC*> sccs(num);
  //        assert(num >= 0);
  unsigned i;
  for (i = 0; i < num; i++) {
    sccs[i] = new StaticRecordSCC();
  }


  assert(vertex2node.size() == component.size());
  for (i = 0; i < component.size(); ++i) {
    assert(i < component.size());
    unsigned c = component[i];
    assert(i < vertex2node.size());
    StaticRecord* rec = vertex2node[i];
    assert(c < sccs.size());
    StaticRecordSCC* scc = sccs[c];
    assert(scc);
    scc->elms.push_back(rec);
    assert(rec);
    rec->scc = scc;
  }

  foreach(it, nodes.begin(), nodes.end()) {
    StaticRecord* n = *it;

    foreach(succit, n->control_succs.begin(), n->control_succs.end()) {
      StaticRecord* succn = *succit;

      if (succn->scc == n->scc) continue;

      n->scc->succs.insert(succn->scc);
      succn->scc->preds.insert(n->scc);
    }
  }

  foreach(it, nodes.begin(), nodes.end()) {
    StaticRecord* rec = *it;
    if (!rec->kfunction->trackCoverage) {
      // rec->cover(rec->function->getNameStr() == "fprintf");
      rec->cover();
    }

  }

  foreach(it, nodes.begin(), nodes.end()) {
    StaticRecord* rec = *it;
    if (!rec->scc->completed) {
      std::cout << "NOT COMPLETED: " << rec->function->getNameStr() << " " << rec->basicBlock->getNameStr() << " " << rec->name() << std::endl;

      foreach(succit, rec->scc->succs.begin(), rec->scc->succs.end()) {
        StaticRecordSCC* scc = *succit;
        if (!scc->completed) {
          std::cout << " " << (scc->completed ? "completed" : "not completed") << " ";
          scc->print();
        }
      }
    }
  }


}

void ControlDependence::propagateControlsExit() {
  std::list<StaticRecord*> worklist;
  std::set<StaticRecord*> visited;

  foreach(it, module->begin(), module->end()) {
    Function* f = &*it;
    if (f->isDeclaration()) continue;
    ACFG* acfg = acfgs[f];
    assert(acfg);

    foreach(it1, acfg->nodes.begin(), acfg->nodes.end()) {
      CDNode* n1 = *it1;
      if (StaticRecordCDNode * srn = n1->toStaticRecord()) {
        StaticRecord* sr = srn->staticRecord;
        if (sr->controlsExit) {
          worklist.push_back(sr);
          visited.insert(sr);
        }
      }
    }
  }

  while (!worklist.empty()) {
    StaticRecord* sr2 = worklist.front();
    //std::cout << "CONTROLS EXIT: " << sr2->basicBlock->getNameStr() << " " << sr2->function->getNameStr() << std::endl;
    worklist.pop_front();

    foreach(it, sr2->control_preds.begin(), sr2->control_preds.end()) {
      StaticRecord* sr1 = *it;
      if (!visited.count(sr1)) {
        sr1->controlsExit = true;
        worklist.push_back(sr1);
        visited.insert(sr1);
      }
    }
  }

}

void ControlDependence::buildACDG() {

  foreach(it, module->begin(), module->end()) {
    Function* f = &*it;
    if (f->isDeclaration()) continue;

    ACFG* acfg = new ACFG(f, recm, aliasingRunner->callgraph);
    acfgs[f] = acfg;
    if (WriteControlGraphs)
      acfg->writeACFGGraph();

    foreach(it1, acfg->nodes.begin(), acfg->nodes.end()) {
      CDNode* n1 = *it1;

      if (StaticRecordCDNode * srn1 = n1->toStaticRecord()) {
        StaticRecord* sr1 = srn1->staticRecord;
        if (sr1->isPredicate()) {
          if (n1->idom->toExit()) {
            sr1->iPostDomIsExit = true;
          } else if (n1->idom->toSuperExit()) {
            sr1->iPostDomIsSuperExit = true;
          } else if (StaticRecordCDNode * sripd = n1->idom->toStaticRecord()) {
            sr1->ipostdom = sripd->staticRecord;
          } else {
            std::cout << "n1=" << n1->name() << " ipdom=" << n1->idom->name() << " fn=" << f->getNameStr() << std::endl;
            assert(false);
          }
        }
      }

      foreach(it2, n1->pcg_succs.begin(), n1->pcg_succs.end()) {
        CDNode* n2 = *it2;

        if (n1->isACDGNode() && n2->isACDGNode()) {
          assert(!n2->toReturnPred());
          n1->addACDGSucc(n2);
        }

        if (n1->toStart() && !n2->toEntry() && !n2->toReturnPred()) {
          assert(!n2->toReturnPred());
          acfg->entry->addACDGSucc(n2);
        }

        if (ReturnPredCDNode * retpredn = n1->toReturnPred()) {
          if (!n2->toReturnPred()) {
            ReturnCDNode* retn = acfg->getReturnNode(retpredn->callinst);
            retn->addACDGSucc(n2);
          }
        }
      }
    }

    if (WriteControlGraphs)
      acfg->writeACDGGraph();

    foreach(it, acfg->nodes.begin(), acfg->nodes.end()) {
      CDNode* n = *it;
      if (StaticRecordCDNode * sr = n->toStaticRecord()) {
        if (isa<UnreachableInst > (sr->staticRecord->insts.back()))
          continue;
      }

      if (n->isACDGNode() && !n->toEntry() && !n->toReturn() && !n->toExit()) {
        if (n->acdg_preds.empty()) {
          std::cout << f->getNameStr() << " " << n->name() << std::endl;
          acfg->writeACFGGraph();
          acfg->writeACDGGraph();
          assert(false);
        }
        /*if (!n->acdg_succs.empty()) {
            std::cout << n->name() << std::endl;
            acfg->writeACFGGraph();
            acfg->writeACDGGraph();
            assert(false);
        }*/
      }

      if (n->toEntry() && n->toReturn()) {
        if (!n->acdg_preds.empty()) {
          std::cout << n->name() << std::endl;
          acfg->writeACFGGraph();
          acfg->writeACDGGraph();
          assert(false);
        }
      }
    }

  }
}

void ControlDependence::findControls() {
  std::set<CDNode*> hasPlaceHolderPred;

  foreach(it, module->begin(), module->end()) {
    Function* f = &*it;
    if (f->isDeclaration()) continue;
    ACFG* acfg = acfgs[f];
    assert(acfg);

    foreach(it1, acfg->nodes.begin(), acfg->nodes.end()) {
      CDNode* n1 = *it1;
      assert(n1);
      if (!n1->isACDGNode()) continue;

      foreach(it2, n1->acdg_succs.begin(), n1->acdg_succs.end()) {

        CDNode* n2 = *it2;
        assert(n2);
        assert(n2->isACDGNode());

        n1->addICFGSucc(n2);
        if (StaticRecordCDNode * srn1 = n1->toStaticRecord()) {
          if (StaticRecordCDNode * srn2 = n2->toStaticRecord()) {
            srn1->staticRecord->addControlSucc(srn2->staticRecord);
          }
        }


        if (n1->isPlaceHolder()) {
          hasPlaceHolderPred.insert(n2);
        }
      }

      if (CallInst * ci = n1->toCallInst()) {
        assert(ci);

        foreach(tarit, aliasingRunner->callgraph->callees_begin(ci), aliasingRunner->callgraph->callees_end(ci)) {
          Function* tar = tarit->second;
          if (tar->isDeclaration()) continue;
          assert(tar);
          ACFG* acfg2 = acfgs[tar];
          assert(acfg2);
          n1->addICFGSucc(acfg2->entry);
          assert(acfg->retnodes.count(ci));
          ReturnCDNode* ret = acfg->retnodes[ci];
          assert(ret);
          acfg2->exit->addICFGSucc(ret);
        }
      }
    }
  }

  foreach(it, hasPlaceHolderPred.begin(), hasPlaceHolderPred.end()) {
    CDNode* m = *it;

    std::set<CDNode*> visited;
    std::list<CDNode*> worklist;

    foreach(it, m->icfg_preds.begin(), m->icfg_preds.end()) {
      CDNode* n = *it;
      worklist.push_back(n);
      visited.insert(n);
    }

    while (!worklist.empty()) {
      CDNode* n = worklist.front();
      worklist.pop_front();

      foreach(it, n->icfg_preds.begin(), n->icfg_preds.end()) {
        CDNode* p = *it;

        if (StaticRecordCDNode * srnp = p->toStaticRecord()) {
          if (StaticRecordCDNode * srnm = m->toStaticRecord()) {
            srnp->staticRecord->addControlSucc(srnm->staticRecord);
          }
        } else {
          if (!visited.count(p)) {
            worklist.push_back(p);
            visited.insert(p);
          }
        }
      }
    }
  }

  /*foreach(it, module->begin(), module->end()) {
      Function* f = &*it;
      if (f->isDeclaration()) continue;
      ACFG* acfg = acfgs[f];
      assert(acfg);

      foreach(it1, acfg->nodes.begin(), acfg->nodes.end()) {
          CDNode* n1 = *it1;
          assert(n1);

          StaticRecordCDNode* srn1 = n1->toStaticRecord();
          if (!srn1) continue;

          StaticRecord* sr1 = srn1->staticRecord;

          foreach(it2, sr1->control_succs.begin(), sr1->control_succs.end()) {
              StaticRecord* sr2 = *it2;
              std::cout << "ctrl: " << n1->name() << ":" << sr1->function->getNameStr() << " " << sr2->name() << ":" << sr2->function->getNameStr() << std::endl;
          }
      }
  }*/
}

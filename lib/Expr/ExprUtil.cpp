//===-- ExprUtil.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/util/ExprUtil.h"
#include "klee/util/ExprHashMap.h"

#include "klee/Expr.h"

#include "klee/util/ExprVisitor.h"

#include "static/Sugar.h"
#include <set>

using namespace klee;

void ExprUtil::findReads(ref<Expr> e,
                     bool visitUpdates,
                     std::vector< ref<ReadExpr> > &results) {
  // Invariant: \forall_{i \in stack} !i.isConstant() && i \in visited
  std::vector< ref<Expr> > stack;
  ExprHashSet visited;
  std::set<const UpdateNode *> updates;

  if (!isa<ConstantExpr>(e)) {
    visited.insert(e);
    stack.push_back(e);
  }

  while (!stack.empty()) {
    ref<Expr> top = stack.back();
    stack.pop_back();

    if (ReadExpr *re = dyn_cast<ReadExpr>(top)) {
      // We memoized so can just add to list without worrying about
      // repeats.
      results.push_back(re);

      if (!isa<ConstantExpr>(re->index) &&
          visited.insert(re->index).second)
        stack.push_back(re->index);

      if (visitUpdates) {
        // XXX this is probably suboptimal. We want to avoid a potential
        // explosion traversing update lists which can be quite
        // long. However, it seems silly to hash all of the update nodes
        // especially since we memoize all the expr results anyway. So
        // we take a simple approach of memoizing the results for the
        // head, which often will be shared among multiple nodes.
        if (updates.insert(re->updates.head).second) {
          for (const UpdateNode *un=re->updates.head; un; un=un->next) {
            if (!isa<ConstantExpr>(un->index) &&
                visited.insert(un->index).second)
              stack.push_back(un->index);
            if (!isa<ConstantExpr>(un->value) &&
                visited.insert(un->value).second)
              stack.push_back(un->value);
          }
        }
      }
    } else if (!isa<ConstantExpr>(top)) {
      Expr *e = top.get();
      for (unsigned i=0; i<e->getNumKids(); i++) {
        ref<Expr> k = e->getKid(i);
        if (!isa<ConstantExpr>(k) &&
            visited.insert(k).second)
          stack.push_back(k);
      }
    }
  }
}

///

namespace klee {

class SymbolicObjectFinder : public ExprVisitor {
protected:
  Action visitRead(const ReadExpr &re) {
    const UpdateList &ul = re.updates;

    // XXX should we memo better than what ExprVisitor is doing for us?
    for (const UpdateNode *un=ul.head; un; un=un->next) {
      visit(un->index);
      visit(un->value);
    }

    if (ul.getRoot()->isSymbolicArray())
      if (results.insert(ul.getRoot().get()).second)
        objects.push_back(ul.getRoot().get());

    return Action::doChildren();
  }

public:
  std::set<const Array*> results;
  std::vector<const Array*> &objects;

  SymbolicObjectFinder(std::vector<const Array*> &_objects)
    : objects(_objects) {}
};

}

template<typename InputIterator>
void ExprUtil::findSymbolicObjects(InputIterator begin,
                               InputIterator end,
                               std::vector<const Array*> &results) {
  SymbolicObjectFinder of(results);
  for (; begin!=end; ++begin)
    of.apply(*begin);
}

void ExprUtil::findSymbolicObjects(
	const ref<Expr>& e,
	std::vector<const Array*> &results)
{ findSymbolicObjects(&e, &e+1, results); }

void ExprUtil::findSymbolicObjectsRef(
	const ref<Expr>& e,
	std::vector<ref<Array> > &results)
{
	std::vector<const Array*>	a;
	findSymbolicObjects(e, a);
	foreach (it, a.begin(), a.end()) {
		results.push_back(
			ref<Array>(const_cast<Array*>(*it)));
	}
}

typedef std::vector< ref<Expr> >::iterator A;
template void klee::ExprUtil::findSymbolicObjects<A>(A, A, std::vector<const Array*> &);

typedef std::set< ref<Expr> >::iterator B;
template void klee::ExprUtil::findSymbolicObjects<B>(B, B, std::vector<const Array*> &);

namespace klee{
class NumNodeCounter : public ExprConstVisitor
{
public:
	NumNodeCounter(unsigned _max) : ExprConstVisitor(false), max(_max) {}
	virtual ~NumNodeCounter() {}
	unsigned getCount(const ref<Expr>& e) { k = 0; apply(e); return k; }
protected:
	virtual Action visitExpr(const Expr* expr)
	{ k++; return (k > max) ? Stop : Expand; }
	unsigned k, max;
};
}
unsigned ExprUtil::getNumNodes(
	const ref<Expr>& e, bool visitUpdates, unsigned max)
{
	NumNodeCounter	nc(max);
	assert (!visitUpdates);
	return nc.getCount(e);
}

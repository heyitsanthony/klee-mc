#include "Executor.h"
#include "ExeStateManager.h"
#include "EquivalentStateEliminator.h"
#include "Searcher.h"
#include "UserSearcher.h"
#include "llvm/System/Process.h"
#include "klee/Common.h"

#include <algorithm>
#include <iostream>

using namespace llvm;
using namespace klee;

ExeStateManager::ExeStateManager()
: nonCompactStateCount(0), searcher(0), equivStateElim(0)
{
}

ExeStateManager::~ExeStateManager()
{
  if (searcher) delete searcher;
  if (equivStateElim) delete equivStateElim;
}

ExecutionState* ExeStateManager::selectState(bool allowCompact)
{
  ExecutionState* ret;

  assert (!empty());
  ret = &searcher->selectState(allowCompact);
  assert((allowCompact || !ret->isCompactForm) && "compact state chosen");

  return ret;
}

void ExeStateManager::setupSearcher(Executor* exe)
{
  assert (!searcher && "Searcher already inited");
  searcher = constructUserSearcher(*exe);
  searcher->update(0, states, ExeStateSet(), ExeStateSet(), ExeStateSet());
}

void ExeStateManager::teardownUserSearcher(void)
{
  assert (searcher);
  if (equivStateElim) equivStateElim->complete();
  delete searcher;
  searcher = 0;
}

void ExeStateManager::getArrayStates(
  unsigned int idx, const std::string& arrName, ExeStateSet& ss)
{
  for (ExeStateSet::iterator it = states.begin(), ite = states.end();
   it != ite; ++it)
  {
    ExecutionState* s = *it;
    ConstraintManager* cons_man = &s->constraints;
    std::set<unsigned int>  indices;

    if (s->isCompactForm) continue;

    for (ConstraintManager::constraint_iterator 
      it = cons_man->begin(), ite = cons_man->end(); 
      it != ite; ++it)
    {
      unsigned int re_idx, cmp_val;
      if (isStrcmpMatch((*it).get(), idx, arrName, re_idx, cmp_val)) {
        ss.insert(s);
        break;
      }
    }
  }
}

/* want expressions of form
 * (Eq <constant> (Read w8 <constant <= idx> arrName)) */
bool ExeStateManager::isStrcmpMatch(
  const Expr  *expr,
  unsigned int idx, const std::string& arrName,
  unsigned int& re_idx, unsigned int& cmp_val)
{
  const EqExpr  *eq_expr;
  const ReadExpr *re;
  const ConstantExpr *idx_expr, *cmp_expr;
  uint64_t  arr_read_idx;

  if (!(eq_expr = dyn_cast<const EqExpr>(expr))) return false;
  if (!(cmp_expr = dyn_cast<const ConstantExpr>(eq_expr->left))) return false;
  if (!(re = dyn_cast<const ReadExpr>(eq_expr->right))) return false;
  if (!(idx_expr = dyn_cast<const ConstantExpr>(re->index))) return false;

  arr_read_idx = idx_expr->getLimitedValue(MERGE_STRMAX);
  if (arr_read_idx > idx) return false; /* out of bounds constraint */
  if (re->updates.root->name != arrName) return false;

  re_idx = arr_read_idx;
  cmp_val = cmp_expr->getLimitedValue(0xffff);  /* 8 bit cmp, should not OF */

  return true;
}

bool ExeStateManager::hasScanStringState(
  ExeStateSet& ss,
  unsigned int idx, const std::string& arrName)
{
  for (ExeStateSet::iterator it = ss.begin(), ite = ss.end();
    it != ite; ++it)
  {
    ExecutionState* s = *it;
    ConstraintManager* cons_man = &s->constraints;
    std::set<unsigned int>  indices;

    if (s->isCompactForm) continue;

    for (ConstraintManager::constraint_iterator 
      it = cons_man->begin(), ite = cons_man->end(); 
      it != ite; ++it)
    {
      unsigned int re_idx, cmp_val;
      if (!isStrcmpMatch((*it).get(), idx, arrName, re_idx, cmp_val)) continue;
      /* TODO: cmp_val should be permitted to be values besides zero */
      if (re_idx == idx  && cmp_val != 0 ) break; /* no null terminal */
      indices.insert(re_idx);
    }

    if (indices.size() == (idx+1)) return true;
  }
  return false;
}

void ExeStateManager::removeStringStates(
  ExeStateSet& ss,
  unsigned int idx, const std::string& arrName)
{
  for (ExeStateSet::iterator it = ss.begin(), ite = ss.end();
    it != ite; ++it)
  {
    ExecutionState* s = *it;
    ConstraintManager* cons_man = &s->constraints;
    std::set<unsigned int>  indices;
    unsigned int run_length;

    if (s->isCompactForm) continue;

    /* find indices that are compared on arrName */
    for (ConstraintManager::constraint_iterator 
      it = cons_man->begin(), ite = cons_man->end(); 
      it != ite; ++it)
    {
      unsigned int re_arr_idx, cmp_val;
      if (!isStrcmpMatch((*it).get(), idx, arrName, re_arr_idx, cmp_val))
        continue;
      indices.insert(re_arr_idx);
    }

    /* verify that the whole thing is filled out */
    for (run_length = 0; run_length < indices.size(); run_length++) {
      if (indices.count(run_length) == 0) break;
    }

    /* there's a gap. don't bother */
    if (run_length < indices.size())  continue;
    /* if there's no elements, don't bother */
    if (run_length == 0) continue;

    /* Search again, want to find (Eq false (Eq n (Read w8 run_length))).
     * This expression terminates the string comparison. */
    for (ConstraintManager::constraint_iterator 
      it = cons_man->begin(), ite = cons_man->end(); 
      it != ite; ++it)
    {
      const EqExpr  *eq_expr_outer;
      const Expr* expr_inner;
      unsigned int re_arr_idx, cmp_val;
      if (!(eq_expr_outer = dyn_cast<const EqExpr>(*it))) continue;
      expr_inner = (eq_expr_outer->right).get();
      if (!isStrcmpMatch(expr_inner, idx, arrName, re_arr_idx, cmp_val))
        continue;
      /* Found match. done. */
      /* remove if the tail is a false comparison, but keep 
       * anything that tests the end of the string */
      if (re_arr_idx == run_length && re_arr_idx != idx && re_arr_idx != 0)
        removedStates.insert(s);
      break;
    }
  }

}

void ExeStateManager::setInitialState(
  Executor* exe,
  ExecutionState* initialState, bool replay)
{
  assert (empty());

  if (replay) {
    // remove initial state from ptree
    states.insert(initialState);
    removedStates.insert(initialState);
    updateStates(exe, NULL); /* XXX ??? */
  } else {
    states.insert(initialState);
    ++nonCompactStateCount;
  }
}

void ExeStateManager::setWeights(double weight)
{
  for (ExeStateSet::iterator it = states.begin(), ie = states.end(); 
    it != ie; ++it)
  {
    (*it)->weight = weight;
  }
}

void ExeStateManager::add(ExecutionState* es)
{
  if (!es->isCompactForm) ++nonCompactStateCount;
  addedStates.insert(es);
}

void ExeStateManager::remove(ExecutionState* s)
{
    removedStates.insert(s);
}

void ExeStateManager::dropAdded(ExecutionState* es)
{
  ExeStateSet::iterator it = addedStates.find(es);
  assert (it != addedStates.end());
  addedStates.erase(it);
}

void ExeStateManager::updateStates(Executor* exe, ExecutionState *current)
{
  if (searcher) {
    searcher->update(current, addedStates, removedStates, ignoreStates, unignoreStates);
    ignoreStates.clear();
    unignoreStates.clear();
  }
  if (equivStateElim) {      
    equivStateElim->update(current, addedStates, removedStates, ignoreStates, unignoreStates);      
  }

  states.insert(addedStates.begin(), addedStates.end());
  nonCompactStateCount += std::count_if(
    addedStates.begin(),
    addedStates.end(),
    std::mem_fun(&ExecutionState::isNonCompactForm_f));
  addedStates.clear();

  ExecutionState* root_to_be_removed = 0;
  for (ExeStateSet::iterator it = removedStates.begin();
       it != removedStates.end(); ++it) {
    ExecutionState *es = *it;

    ExeStateSet::iterator it2 = states.find(es);
    assert(it2!=states.end());
    states.erase(it2);

    // this deref must happen before delete
    if (!es->isCompactForm) --nonCompactStateCount;
    exe->removePTreeState(es, &root_to_be_removed);
  }

  ExecutionState* es = root_to_be_removed;
  if (es) exe->removeRoot(es);
  removedStates.clear();
  replacedStates.clear();

//  klee_message("Updated to: %s\n", states2str(states).c_str());
}

void ExeStateManager::replaceState(ExecutionState* old_s, ExecutionState* new_s)
{
  addedStates.insert(new_s);
  removedStates.insert(old_s);
  replacedStates[old_s] = new_s;
}

/* don't bother queueing up state for deletion, get rid of it immediately */
void ExeStateManager::replaceStateImmediate(
  ExecutionState* old_s, ExecutionState* new_s)
{
  assert (!isRemovedState(new_s));
  addedStates.insert(new_s);
  assert (isAddedState(old_s));
  addedStates.erase(old_s);
  replacedStates[old_s] = new_s;
}

bool ExeStateManager::isAddedState(ExecutionState* s) const
{
  return (addedStates.count(s) > 0);
}

bool ExeStateManager::isRemovedState(ExecutionState* s) const
{
  return (removedStates.count(s) > 0);
}

ExecutionState* ExeStateManager::getReplacedState(ExecutionState* s) const
{
  ExeStateReplaceMap::const_iterator it;
  it = replacedStates.find(s);
  if (it != replacedStates.end()) return it->second;
  return NULL;
}

void ExeStateManager::compactStates(ExecutionState* &state, uint64_t maxMem)
{
  // compact instead of killing
  std::vector<ExecutionState*> arr(nonCompactStateCount);
  unsigned i = 0;
  for (ExeStateSet::iterator si = states.begin(); si != states.end(); ++si) {
    if((*si)->isCompactForm) continue;
    arr[i++] = *si;
  }

   // a rough measure
  unsigned s = nonCompactStateCount + ((size()-nonCompactStateCount)/16);
  unsigned mbs = sys::Process::GetTotalMemoryUsage() >> 20;
  unsigned toCompact = std::max(
    (uint64_t)1, (uint64_t)s - s * maxMem / mbs);
  toCompact = std::min(toCompact, (unsigned) nonCompactStateCount);
  klee_warning("compacting %u states (over memory cap)", toCompact);

  std::partial_sort(
    arr.begin(), arr.begin() + toCompact, arr.end(), KillOrCompactOrdering());

  for (i = 0; i < toCompact; ++i) {
    ExecutionState* original = arr[i];
    ExecutionState* compacted = original->compact();
    compacted->coveredNew = false;
    compacted->coveredLines.clear();
    compacted->ptreeNode = original->ptreeNode;
    replaceState(original, compacted);
    if (state == original) state = compacted;
  }
}

void ExeStateManager::setupESE(
  Executor* exe, KModule* kmodule, ExecutionState* state)
{
  equivStateElim = new EquivalentStateEliminator(exe, kmodule, states);
  ExeStateSet tmp;
  equivStateElim->setup(state, tmp);
  assert(tmp.empty());
}

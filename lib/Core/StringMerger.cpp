/* When new states are added/remove, track array constraints. */
/* */
#include "klee/Common.h"
#include "StringMerger.h"
#include "static/Sugar.h"

using namespace klee;


#define MERGE_STRMAX  1024

StringMerger::~StringMerger(void)
{
  foreach(it, arrStates.begin(), arrStates.end()) {
    ExeStateSet*  ess = it->second;
    if (ess) delete ess; 
  }
  delete baseSearcher;
}

bool StringMerger::isArrCmp(
  const Expr* expr, std::string& arrName,
  uint64_t& arr_idx, uint8_t& cmp_val) const
{
  const EqExpr  *eq_expr;
  const ReadExpr *re;
  const ConstantExpr *idx_expr, *cmp_expr;

  if (!(eq_expr = dyn_cast<const EqExpr>(expr))) return false;
  if (!(cmp_expr = dyn_cast<const ConstantExpr>(eq_expr->left))) return false;
  if (!(re = dyn_cast<const ReadExpr>(eq_expr->right))) return false;
  if (!(idx_expr = dyn_cast<const ConstantExpr>(re->index))) return false;

  /* don't overflow */
  arr_idx = idx_expr->getLimitedValue(MERGE_STRMAX);
  if (arr_idx == MERGE_STRMAX) return false;

  arrName = re->updates.root->name;
  cmp_val = cmp_expr->getLimitedValue(0xffff);  /* 8 bit cmp, should not OF */

  return true;
}
  
/* 0 => no run length! */
int StringMerger::runLength(const std::vector<const Expr*>& exprs) const
{
  std::set<uint64_t>  indices;

  foreach (it, exprs.begin(), exprs.end()) {
    std::string arr_name;
    uint64_t arr_idx;
    uint8_t cmp_val;

    if (!isArrCmp(*it, arr_name, arr_idx, cmp_val)) {
      assert (0 == 1 && "NOT ARR CMP??");
      continue;
    }

    indices.insert(arr_idx);
  }

  /* check for gaps */
  for (unsigned int i = 0; i < indices.size(); i++)
    if (!indices.count(i))
      return 0;

  return indices.size();
}

/* handle an added state from update */
void StringMerger::addState(ExecutionState* s)
{
  ArrExprMap  arr_exprs;
  buildArrExprList(s, arr_exprs);

  foreach (it, arr_exprs.begin(), arr_exprs.end()) {
    ExeStateSet* arr_states;
    std::string arr_name(it->first);

    arr_states = arrStates[arr_name]; 
    if (!arr_states) {
      arr_states = new ExeStateSet();
      arrStates[arr_name] = arr_states;
    }
    arr_states->insert(s);
  }
  states.insert(s);
}

/* handle a removed state from update */
void StringMerger::removeState(ExecutionState* s)
{
  ArrExprMap  arr_exprs;
  buildArrExprList(s, arr_exprs);

  foreach (it, arr_exprs.begin(), arr_exprs.end()) {
    ExeStateSet* arr_states;
    std::string arr_name(it->first);

    arr_states = arrStates[arr_name]; 
    assert (arr_states && "Removing a state we don't have??");

    arr_states->erase(s);
    if (arr_states->size() != 0) continue;

    /* no states left for this array type. remove set */
    delete arr_states;
    arrStates.erase(arr_name);
  }
  ascendingStates.erase(s);
  states.erase(s);
}

/* get all constraints that are u8 array cmps */
void StringMerger::buildArrExprList(
  ExecutionState* s, ArrExprMap& arr_exprs) const
{
  ConstraintManager* cons_man = &s->constraints;

  if (s->isCompact()) return;

  foreach (it, cons_man->begin(), cons_man->end()) {
    std::string arr_name;
    uint64_t arr_idx;
    uint8_t cmp_val;

    if (!isArrCmp((*it).get(), arr_name, arr_idx, cmp_val)) continue;
    arr_exprs[arr_name].push_back((*it).get());
  }
}

#if 0
/* FIXME this is really slow. */
void Executor::mergeStringStates(ref<Expr>& readExpr)
{
  const ReadExpr      *re;
  const ConstantExpr  *ce_idx;
  std::string   arr_name;
  uint64_t      idx;

  /* Make sure incoming comparison has a symbolic byte read */
  if (!(re = dyn_cast<const ReadExpr>(readExpr))) return;

  /* Only handle constant indices for now. Use solver for general expressions. */
  ce_idx = dyn_cast<const ConstantExpr>(re->index);
  if (ce_idx == NULL) return;
  
  /* Only bother with strings with more than one character */
  idx = ce_idx->getLimitedValue(MERGE_STRMAX);
  if (idx <= 1 || idx == MERGE_STRMAX) return;
  
  arr_name = re->updates.root->name;

  ExeStateSet ss;
  stateManager->getArrayStates(idx, arr_name, ss);

  /* kekekekeke */
  /* find expressions of form (Eq x (Read w8 y arr_name)) */
  if (stateManager->hasScanStringState(ss, idx, arr_name)) {
    stateManager->removeStringStates(ss, idx, arr_name);
  }
}
#endif

/* if current's array constraints follow ... */
void StringMerger::subsume(
  ExecutionState* current,
  const std::string& arr_name,
  const std::vector<const Expr*> arr_exprs,
  ExeStateSet& subsumed)
{
  ExeStateSet *matching_states = arrStates[arr_name];
  klee_message("SUBSUME ME");
  foreach (it2, matching_states->begin(), matching_states->end()) {
    ArrExprMap      aem;
    ExecutionState  *s;
    unsigned int    run_len;

    s = *it2;
    buildArrExprList(s, aem);  /* XXX slow */
    run_len = runLength(aem[arr_name]);
    klee_message("RUN_LEN: %d", run_len);
    ascendingStates.insert(s);
    subsumed.insert(s);
  }
}

/* knock out all states that are ascending */
void StringMerger::removeAscenders(
  ExecutionState* current, ExeStateSet& removed)
{
  ArrExprMap  arr_exprs;
  unsigned int  max_run;

  assert (current != NULL);
  buildArrExprList(current, arr_exprs);

  /* cycle through arrays with runs >=2 (string data!) */
  foreach (it, arr_exprs.begin(), arr_exprs.end()) {
    max_run = runLength(it->second);
    if (max_run < 2) continue;
    /* cycle through known states that contain matching runs */
    klee_message("SUBSUMING LEN=%d", max_run);
    subsume(current, it->first, it->second, removed);
  }
}

void StringMerger::update(ExecutionState *current, const States s)
{
  ExeStateSet removedAscending;

  foreach (it, s.getAdded().begin(), s.getAdded().end()) addState(*it);
  foreach (it, s.getRemoved().begin(), s.getRemoved().end()) removeState(*it);
  
  if (current) removeAscenders(current, removedAscending);

  if (removedAscending.size() != 0) {
    /* need to change the removed/added if we removed something */
    ExeStateSet newRemoved, newAdded;

    newRemoved = s.getRemoved();
    newAdded = s.getAdded();
    foreach(it, removedAscending.begin(), removedAscending.end()) {
      ExecutionState* es = *it;
      if (newAdded.count(es)) newAdded.erase(es);
      else newRemoved.insert(*it);
    }

    baseSearcher->update(current, States(newAdded, newRemoved));
    return;
  }

  baseSearcher->update(current, s);
}

ExecutionState& StringMerger::selectState(bool allowCompact)
{
  return baseSearcher->selectState(allowCompact);
}

bool StringMerger::empty(void) const
{ 
  return states.empty();
}

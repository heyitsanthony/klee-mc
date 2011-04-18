//===-- AddressSpace.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AddressSpace.h"
#include "CoreStats.h"
#include "Memory.h"
#include "TimingSolver.h"
#include "StateRecord.h"
#include "klee/Expr.h"
#include "klee/TimerStatIncrementer.h"

#include <stack>

using namespace klee;

///

void AddressSpace::bindObject(const MemoryObject *mo, ObjectState *os) {
  assert(os->copyOnWriteOwner == 0 && "object already has owner");
  os->copyOnWriteOwner = cowKey;
  objects = objects.replace(std::make_pair(mo, os));
}

void AddressSpace::unbindObject(const MemoryObject *mo) {
  objects = objects.remove(mo);
}

const ObjectState *AddressSpace::findObject(const MemoryObject *mo) const {
  const MemoryMap::value_type *res = objects.lookup(mo);

  return res ? res->second : 0;
}

ObjectState *AddressSpace::getWriteable(const MemoryObject *mo,
        const ObjectState *os) {
  assert(!os->readOnly);

  if (cowKey == os->copyOnWriteOwner) {
    return const_cast<ObjectState*> (os);
  } else {
    ObjectState *n = new ObjectState(*os);
    n->copyOnWriteOwner = cowKey;
    objects = objects.replace(std::make_pair(mo, n));
    return n;
  }
}

/// 

bool AddressSpace::resolveOne(const ref<ConstantExpr> &addr,
        ObjectPair &result) {
  uint64_t address = addr->getZExtValue();
  MemoryObject hack(address);

  if (const MemoryMap::value_type * res = objects.lookup_previous(&hack)) {
    const MemoryObject *mo = res->first;
    if ((mo->size == 0 && address == mo->address) ||
            (address - mo->address < mo->size)) {
      result = *res;
      return true;
    }
  }

  return false;
}

bool AddressSpace::resolveOne(ExecutionState &state,
        TimingSolver *solver,
        ref<Expr> address,
        ObjectPair &result,
        bool &success) {
  if (ConstantExpr * CE = dyn_cast<ConstantExpr > (address)) {
    success = resolveOne(CE, result);
    return true;
  } else {
    TimerStatIncrementer timer(stats::resolveTime);

    // try cheap search, will succeed for any inbounds pointer

    ref<ConstantExpr> cex;
    if (!solver->getValue(state, address, cex))
      return false;
    uint64_t example = cex->getZExtValue();
    MemoryObject hack(example);
    const MemoryMap::value_type *res = objects.lookup_previous(&hack);

    if (res) {
      const MemoryObject *mo = res->first;
      if (example - mo->address < mo->size) {
        result = *res;
        success = true;
        return true;
      }
    }

    // didn't work, now we have to search

    MemoryMap::iterator oi = objects.upper_bound(&hack);
    MemoryMap::iterator gt = oi;
    if (gt == objects.end())
      --gt;
    else
      ++gt;
    MemoryMap::iterator lt = oi;
    if (lt != objects.begin())
      --lt;

    MemoryMap::iterator begin = objects.begin();
    MemoryMap::iterator end = objects.end();
    --end;

    std::pair<MemoryMap::iterator, MemoryMap::iterator> left(begin,lt);
    std::pair<MemoryMap::iterator, MemoryMap::iterator> right(gt,end);

    while (true) {
      // Check whether current range of MemoryObjects is feasible
      unsigned i = 0;
      for (; i < 2; i++) {
        std::pair<MemoryMap::iterator, MemoryMap::iterator> cur =
          i ? right : left;
        const MemoryObject *low = cur.first->first;
        const MemoryObject *high = cur.second->first;
        bool mayBeTrue;
        ref<Expr> inRange =
          AndExpr::create(UgeExpr::create(address, low->getBaseExpr()),
                          UltExpr::create(address,
                                          AddExpr::create(high->getBaseExpr(),
                                                          high->getSizeExpr())));
        if (!solver->mayBeTrue(state, inRange, mayBeTrue))
          return false;
        if (mayBeTrue) {
          // range is feasible

          // range only contains one MemoryObject, which was proven feasible, so
          // return it
          if (low == high) {
            result = *cur.first;
            success = true;
            return true;
          }

          // range contains more than one object, so divide in half and continue
          // search

          // find the midpoint between ei and bi
          MemoryMap::iterator mid = cur.first;
          bool even = true;
          for (MemoryMap::iterator it = cur.first; it != cur.second; ++it) {
            even = !even;
            if (even)
              ++mid;
          }

          left = std::make_pair(cur.first, mid);
          right = std::make_pair(++mid, cur.second);
          break; // out of for loop
        }
      }

      // neither range was feasible
      if (i == 2) {
        success = false;
        return true;
      }
    }

    success = false;
    return true;
  }
}

bool AddressSpace::resolve(ExecutionState &state,
        TimingSolver *solver,
        ref<Expr> p,
        ResolutionList &rl,
        unsigned maxResolutions,
        double timeout) {
  if (ConstantExpr * CE = dyn_cast<ConstantExpr > (p)) {
    ObjectPair res;
    if (resolveOne(CE, res))
      rl.push_back(res);
    return false;
  } else {
    TimerStatIncrementer timer(stats::resolveTime);
    uint64_t timeout_us = (uint64_t) (timeout * 1000000.);

    // XXX in general this isn't exactly what we want... for
    // a multiple resolution case (or for example, a \in {b,c,0})
    // we want to find the first object, find a cex assuming
    // not the first, find a cex assuming not the second...
    // etc.

    // XXX how do we smartly amortize the cost of checking to
    // see if we need to keep searching up/down, in bad cases?
    // maybe we don't care?

    // XXX we really just need a smart place to start (although
    // if its a known solution then the code below is guaranteed
    // to hit the fast path with exactly 2 queries). we could also
    // just get this by inspection of the expr.

    // DAR: replaced original O(N) lookup with O(log N) binary search strategy

    // Iteratively divide set of MemoryObjects into halves and check whether
    // any feasible address exists in each range. If so, continue iterating.
    // Otherwise, abandon that range of addresses.

    ref<ConstantExpr> cex;
    if (!solver->getValue(state, p, cex))
      return true;
    uint64_t example = cex->getZExtValue();
    MemoryObject hack(example);

    MemoryMap::iterator oi = objects.find(&hack);
    MemoryMap::iterator gt = oi;
    if (gt != objects.end())
      ++gt;
    MemoryMap::iterator lt = oi;
    if (lt != objects.begin())
      --lt;

    MemoryMap::iterator end = objects.end();
    --end;

    // Explicit stack to avoid recursion
    std::stack < std::pair<typeof (oi), typeof (oi)> > tryRanges;

    // Search [begin, first object < example] if range is not empty
    if (lt != objects.begin())
      tryRanges.push(std::make_pair(objects.begin(), lt));
    // Search [first object > example, end-1] if range is not empty
    if (gt != objects.end())
      tryRanges.push(std::make_pair(gt, end));
    // Search [example,example] if exists (may not on weird overlap cases)
    // NOTE: check the example first in case of fast path, so push onto stack
    // last
    if (oi != objects.end())
      tryRanges.push(std::make_pair(oi, oi));

    // Iteratively perform binary search until stack is empty
    while (!tryRanges.empty()) {
      if (timeout_us && timeout_us < timer.check())
        return true;

      MemoryMap::iterator bi = tryRanges.top().first;
      MemoryMap::iterator ei = tryRanges.top().second;
      const MemoryObject *low = bi->first;
      const MemoryObject *high = ei->first;
      tryRanges.pop();

      // Check whether current range of MemoryObjects is feasible
      bool mayBeTrue;
      ref<Expr> inRange =
        AndExpr::create(UgeExpr::create(p, low->getBaseExpr()),
                        UltExpr::create(p,
                                        AddExpr::create(high->getBaseExpr(),
                                                        high->getSizeExpr())));
      if (!solver->mayBeTrue(state, inRange, mayBeTrue))
        return true; // query error
      if (mayBeTrue) {
        // range is feasible

        if (low == high) {
          // range only contains one MemoryObject, which was proven feasible, so
          // add to resolution list
          rl.push_back(*bi);

          // fast path check
          unsigned size = rl.size();
          if (size == 1) {
            bool mustBeTrue;
            if (!solver->mustBeTrue(state, inRange, mustBeTrue))
              return true;
            if (mustBeTrue)
              return false;
          } else if (size == maxResolutions) {
            return true;
          }
        } else {
          // range contains more than one object, so divide in half and push
          // halves onto stack

          // find the midpoint between ei and bi
          MemoryMap::iterator mid = bi;
          bool even = true;
          for (MemoryMap::iterator it = bi; it != ei; ++it) {
            even = !even;
            if (even)
              ++mid;
          }
          tryRanges.push(std::make_pair(bi, mid));
          tryRanges.push(std::make_pair(++mid, ei));
        }
      }
    }
  }
  return false;
}

// These two are pretty big hack so we can sort of pass memory back
// and forth to externals. They work by abusing the concrete cache
// store inside of the object states, which allows them to
// transparently avoid screwing up symbolics (if the byte is symbolic
// then its concrete cache byte isn't being used) but is just a hack.

void AddressSpace::copyOutConcretes() {
  for (MemoryMap::iterator it = objects.begin(), ie = objects.end();
          it != ie; ++it) {
    const MemoryObject *mo = it->first;

    if (!mo->isUserSpecified) {
      ObjectState *os = it->second;
      uint8_t *address = (uint8_t*) (unsigned long) mo->address;

      if (!os->readOnly)
        memcpy(address, os->concreteStore, mo->size);
    }
  }
}

bool AddressSpace::copyInConcretes(StateRecord* rec) {
  for (MemoryMap::iterator it = objects.begin(), ie = objects.end();
          it != ie; ++it) {
    const MemoryObject *mo = it->first;

    if (!mo->isUserSpecified) {
      const ObjectState *os = it->second;
      uint8_t *address = (uint8_t*) (unsigned long) mo->address;

      if (memcmp(address, os->concreteStore, mo->size) != 0) {
        if (os->readOnly) {
          return false;
        } else {
          ObjectState *wos = getWriteable(mo, os);
          if (rec) {            
            for (unsigned i = 0; i < wos->size; ++i) {
              if (wos->isByteConcrete(i)) {
                if (address[i] != wos->concreteStore[i]) {
                  rec->conOffObjectWrite(wos, i, ConstantExpr::create(address[i],Expr::Int8));
                }
              }
            }
          }

          memcpy(wos->concreteStore, address, mo->size);
        }
      }
    }
  }

  return true;
}

/***/

bool MemoryObjectLT::operator()(const MemoryObject *a, const MemoryObject *b) const {
  return a->address < b->address;
}


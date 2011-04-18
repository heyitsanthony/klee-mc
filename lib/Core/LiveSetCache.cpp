#include "LiveSetCache.h"
#include "klee/ExecutionState.h"
#include "StateRecord.h"
#include "DependenceNode.h"
#include "Sugar.h"
#include "Memory.h"
#include <boost/functional/hash.hpp>

using namespace boost;
using namespace klee;

LiveSetCache::LiveSetCache() : compares(0), visits(0) {

}

unsigned LiveSetCache::hash(ref<Expr> e) {
  return e.isNull() ? 0 : e->hash();
}

void LiveSetCache::readd(StateRecord* rec) {
  std::map<unsigned, LiveSet*>::iterator it = cache.find(rec->lochash);
  assert(it != cache.end());
  LiveSet* ls = it->second;

  std::map<unsigned, std::set<StateRecord*> >::iterator it2 = ls->recs.find(rec->valhash);
  assert(it2 != ls->recs.end());

  std::set<StateRecord*>& s = it2->second;

  assert(s.count(rec));

  s.erase(rec);

  add(rec);
}

void LiveSetCache::add(StateRecord* rec) {  

  std::set<DependenceNode*, DependenceNodeLessThan> liveReads;

  foreach(it, rec->liveReads.begin(), rec->liveReads.end()) {
    DependenceNode* n = *it;
    liveReads.insert(n);
  }

  size_t lochash = 0;
  size_t valhash = 0;

  //std::cout << "RECORD: " << rec << std::endl;

  foreach(it, liveReads.begin(), liveReads.end()) {
    DependenceNode* n = *it;
    if (StackWrite * sw = n->toStackWrite()) {
      unsigned h = hash(sw->value);
      boost::hash_combine(valhash, h);
      //      std::cout << "stackwrite: total=" << valhash << " this=" << h  << std::endl;
    } else if (ConOffObjectWrite * co = n->toConOffObjectWrite()) {
      unsigned h = hash(co->value);
      boost::hash_combine(valhash, h);
      //    std::cout << "conoffobjectwrite: total=" << valhash<< " " << h << std::endl;// << " val=" << *(co->value) << " val.hash=" << co->value->hash() << std::endl;
    } else if (n->toSymOffObjectWrite()) {
      //   std::cout << "symoffobjectwrite: " << n->valhash << std::endl;
      boost::hash_combine(valhash, 1);
    } else if (n->toSymOffArrayAlloc()) {
      // std::cout << "arrayalloc: " << n->valhash << std::endl;
      boost::hash_combine(valhash, 1);
    } else if (n->toConOffArrayAlloc()) {
      // std::cout << "arrayalloc: " << n->valhash << std::endl;
      boost::hash_combine(valhash, 1);
    } else {
      assert(false);
    }

    boost::hash_combine(lochash, n->lochash);
  }

  unsigned i = 0;

  foreach(it, rec->liveConstraints.begin(), rec->liveConstraints.end()) {
    ref<Expr> e = *it;
    //  std::cout << "constraint: count=" << i << " " << e->hash() << std::endl;
    boost::hash_combine(valhash, e->hash());
    i++;
  }

  LiveSet* ls = cache[lochash];

  if (!ls) {
    ls = new LiveSet();
    cache[lochash] = ls;

    foreach(it, liveReads.begin(), liveReads.end()) {
      DependenceNode* n = *it;
      if (StackWrite * sw = n->toStackWrite()) {
        StackLocation loc(sw->sfi, sw->reg);
        ls->stackLocations.push_back(loc);
        stackLocUnion[loc].insert(hash(sw->value));
      } else if (ConOffObjectWrite * co = n->toConOffObjectWrite()) {
        ObjectByteLocation loc(co->mallocKey, co->offset);
        ls->objByteLocations.push_back(loc);
        objByteUnion[loc].insert(hash(co->value));
      } else if (SymOffObjectWrite * so = n->toSymOffObjectWrite()) {
        ls->objLocations.push_back(ObjectLocation(so->mallocKey));
      } else if (ConOffArrayAlloc * arr = n->toConOffArrayAlloc()) {
        ls->arrByteLocations.push_back(ArrayByteLocation(arr->mallocKeyOffset));
      } else if (SymOffArrayAlloc * arr = n->toSymOffArrayAlloc()) {
        ls->arrLocations.push_back(ArrayLocation(arr->mallocKey));
      } else {
        assert(false);
      }
    }

    liveSets.insert(ls);
  }

  //std::cout << "valhash=" << valhash << std::endl;
  rec->lochash = lochash;
  rec->valhash = valhash;
  ls->recs[valhash].insert(rec);
}

/*
        StateRecord* check(ExecutionState* state) {

            foreach(it, recs.begin(), recs.end()) {
                StateRecord* trec = *it;
                if (trec->isEquiv(state)) {
                    std::cout << "PRUNING REC: " << trec << " " << trec->inst->getParent()->getParent()->getNameStr() << " " << trec->inst->getParent()->getNameStr() << " " << *(trec->inst) << std::endl;
                    return trec;
                }
            }

            return NULL;
        }
 */

StateRecord* LiveSetCache::check(ExecutionState* state) {

  /*if ((state->pc->inst->getParent()->getParent()->getNameStr() == "ttyname_r") &&
          (state->pc->inst->getParent()->getNameStr() == "bb11")) {

    if (copy) {
      bool diff = false;

      for (unsigned sfi = 0; sfi < state->stack.size(); sfi++) {
        for (unsigned i = 0; i < state->stack[sfi].kf->numRegisters; i++) {
          if (!Support::eq(state->stack[sfi].locals[i].value, copy->stack[sfi].locals[i].value)) {
            diff = true;
            std::cout << "DIFF: fn=" << state->stack[sfi].kf->function->getNameStr() << " v=" << *(state->stack[sfi].kf->getValueForRegister(i)) << " ";
            std::cout << Support::str(copy->stack[sfi].locals[i].value) << " ";
            std::cout << Support::str(state->stack[sfi].locals[i].value) << std::endl;
          }
        }
      }

      if (!(copy->constraints == state->constraints)) {
        diff = true;

        std::set<ref<Expr> > copyc, statec;

        foreach(cit, copy->constraints.begin(), copy->constraints.end()) {
          ref<Expr> e = *cit;
          copyc.insert(e);
        }

        foreach(cit, state->constraints.begin(), state->constraints.end()) {
          ref<Expr> e = *cit;
          statec.insert(e);
        }

        foreach(cit, copyc.begin(), copyc.end()) {
          ref<Expr> e = *cit;
          if (!statec.count(e)) {
            std::cout << "not found in new=" << Support::str(e) << std::endl;
          }
        }

        foreach(cit, statec.begin(), statec.end()) {
          ref<Expr> e = *cit;
          if (!copyc.count(e)) {
            std::cout << "not found in old=" << Support::str(e) << std::endl;
          }
        }

        std::cout << "DIFF: constraints copysize=" << copy->constraints.size() << " statesize" << state->constraints.size() << std::endl;
      }
    }


  }*/

  foreach(it, stackLocUnion.begin(), stackLocUnion.end()) {
    const StackLocation& loc = it->first;
    const std::set<unsigned>& s = it->second;

    StackWrite* sw = state->stack[loc.sfi].locals[loc.reg].stackWrite;
    if (sw && !s.count(hash(sw->value))) {
      //std::cout << "FAIL" << std::endl;
      return NULL;
    }
  }

  foreach(it, objByteUnion.begin(), objByteUnion.end()) {
    const ObjectByteLocation& loc = it->first;
    const std::set<unsigned>& s = it->second;

    const ObjectState* objectState = state->getObjectState(loc.mallocKey);
    if (objectState) {
      ref<Expr> value = objectState->read8(loc.offset, NULL);
      if (!s.count(hash(value))) {
        //std::cout << "FAIL" << std::endl;
        return NULL;
      }
    }
  }

  visits++;

  //  foreach(it, liveSets.begin(), liveSets.end()) {
  //  LiveSet* ls = *it;

  foreach(it, cache.begin(), cache.end()) {
    LiveSet* ls = it->second;

    std::set<MallocKey> liveMallocKeys;
    std::set<MallocKeyOffset> liveMallocKeyOffsets;

    size_t valhash = 0;

    foreach(swit, ls->stackLocations.begin(), ls->stackLocations.end()) {
      StackLocation& p = *swit;
      ref<Expr> value = state->getLocalCell(p.sfi, p.reg).value;
      unsigned h = value.isNull() ? 0 : value->hash();
      boost::hash_combine(valhash, h);
    }

    foreach(swit, ls->objByteLocations.begin(), ls->objByteLocations.end()) {
      ObjectByteLocation& p = *swit;
      const ObjectState* objectState = state->getObjectState(p.mallocKey);
      unsigned h;
      if (objectState) {
        ref<Expr> value = objectState->read8(p.offset, NULL);
        h = value.isNull() ? 0 : value->hash();
        boost::hash_combine(valhash, h);
      } else {
        //TODO: this should return NULL;
        h = 0;
        boost::hash_combine(valhash, h);
      }

      liveMallocKeyOffsets.insert(MallocKeyOffset(p.mallocKey, p.offset));
    }

    foreach(swit, ls->objLocations.begin(), ls->objLocations.end()) {
      ObjectLocation& p = *swit;
      liveMallocKeys.insert(p.mallocKey);
      boost::hash_combine(valhash, 1);
    }

    foreach(swit, ls->arrByteLocations.begin(), ls->arrByteLocations.end()) {
      ArrayByteLocation& p = *swit;
      liveMallocKeyOffsets.insert(p.mallocKeyOffset);
      boost::hash_combine(valhash, 1);
    }

    foreach(swit, ls->arrLocations.begin(), ls->arrLocations.end()) {
      ArrayLocation& p = *swit;
      liveMallocKeys.insert(p.mallocKey);
      boost::hash_combine(valhash, 1);
    }

    std::vector<ref<Expr> > constraints;
    StateRecord::getLiveConstraints(state->constraints, liveMallocKeyOffsets, liveMallocKeys, constraints);
    //StateRecord::getLiveConstraints(state->como2cn, liveMallocKeyOffsets, constraints);
    //StateRecord::getLiveConstraints(state->somo2cn, liveMallocKeys, constraints);

    unsigned i = 0;

    foreach(it, constraints.begin(), constraints.end()) {
      ref<Expr> e = *it;
      boost::hash_combine(valhash, e->hash());
      i++;
    }

    if (ls->recs.count(valhash)) {

      foreach(trecit, ls->recs[valhash].begin(), ls->recs[valhash].end()) {
        StateRecord* trec = *trecit;

        if (trec->isEquiv(state)) {
          //   if (state->pc->inst->getParent()->getNameStr() == "bb68.us")
          //    trec->printPathToExit();

          return trec;
        }
      }
    }

  }

  return NULL;
}

struct StackLocationCountLessThan {

  bool operator() (const std::pair<StackLocation, unsigned>& p1, const std::pair<StackLocation, unsigned>& p2) {
    return p1.second < p2.second;
  }
};

struct ObjectByteLocationCountLessThan {

  bool operator() (const std::pair<ObjectByteLocation, unsigned>& p1, const std::pair<ObjectByteLocation, unsigned>& p2) {
    return p1.second < p2.second;
  }
};

struct ObjectLocationCountLessThan {

  bool operator() (const std::pair<ObjectLocation, unsigned>& p1, const std::pair<ObjectLocation, unsigned>& p2) {
    return p1.second < p2.second;
  }
};

struct ArrayByteLocationCountLessThan {

  bool operator() (const std::pair<ArrayByteLocation, unsigned>& p1, const std::pair<ArrayByteLocation, unsigned>& p2) {
    return p1.second < p2.second;
  }
};

struct ArrayLocationCountLessThan {

  bool operator() (const std::pair<ArrayLocation, unsigned>& p1, const std::pair<ArrayLocation, unsigned>& p2) {
    return p1.second < p2.second;
  }
};

void LiveSetCache::printLiveSetStats() {

  std::map<StackLocation, unsigned> slcount;
  std::map<ObjectByteLocation, unsigned> obcount;
  std::map<ObjectLocation, unsigned> ocount;
  std::map<ArrayByteLocation, unsigned> abcount;
  std::map<ArrayLocation, unsigned> acount;

  foreach(it, cache.begin(), cache.end()) {
    LiveSet* ls = it->second;

    foreach(slit, ls->stackLocations.begin(), ls->stackLocations.end()) {
      slcount[*slit]++;
    }

    foreach(obit, ls->objByteLocations.begin(), ls->objByteLocations.end()) {
      obcount[*obit]++;
    }

    foreach(oit, ls->objLocations.begin(), ls->objLocations.end()) {
      ocount[*oit]++;
    }

    foreach(ait, ls->arrByteLocations.begin(), ls->arrByteLocations.end()) {
      abcount[*ait]++;
    }

    foreach(ait, ls->arrLocations.begin(), ls->arrLocations.end()) {
      acount[*ait]++;
    }
  }


  std::set<std::pair<StackLocation, unsigned>, StackLocationCountLessThan > sllist;
  std::set<std::pair<ObjectByteLocation, unsigned>, ObjectByteLocationCountLessThan > oblist;
  std::set<std::pair<ObjectLocation, unsigned>, ObjectLocationCountLessThan > olist;
  std::set<std::pair<ArrayByteLocation, unsigned>, ArrayByteLocationCountLessThan > ablist;
  std::set<std::pair<ArrayLocation, unsigned>, ArrayLocationCountLessThan > alist;

  foreach(it, slcount.begin(), slcount.end()) {
    sllist.insert(std::make_pair(it->first, it->second));
  }

  foreach(it, obcount.begin(), obcount.end()) {
    oblist.insert(std::make_pair(it->first, it->second));
  }

  foreach(it, ocount.begin(), ocount.end()) {
    olist.insert(std::make_pair(it->first, it->second));
  }

  foreach(it, abcount.begin(), abcount.end()) {
    ablist.insert(std::make_pair(it->first, it->second));
  }

  foreach(it, acount.begin(), acount.end()) {
    alist.insert(std::make_pair(it->first, it->second));
  }

  foreach(it, sllist.begin(), sllist.end()) {
    StackLocation sl = it->first;
    unsigned c = it->second;
    std::cout << "stack: " << (c * 1.0 / cache.size()) << " sfi=" << sl.sfi << " reg=" << sl.reg << std::endl;
  }

  foreach(it, oblist.begin(), oblist.end()) {
    ObjectByteLocation sl = it->first;
    unsigned c = it->second;
    std::cout << "objbyte: " << (c * 1.0 / cache.size()) << " as=" << *(sl.mallocKey.allocSite) << " iter=" << sl.mallocKey.iteration << " offset=" << sl.offset << std::endl;
  }

  foreach(it, olist.begin(), olist.end()) {
    ObjectLocation sl = it->first;
    unsigned c = it->second;
    std::cout << "object: as=" << (c * 1.0 / cache.size()) << " as=" << *(sl.mallocKey.allocSite) << " iter=" << sl.mallocKey.iteration << std::endl;
  }

  foreach(it, ablist.begin(), ablist.end()) {
    ArrayByteLocation sl = it->first;
    unsigned c = it->second;
    std::cout << "arraybyte: " << (c * 1.0 / cache.size()) << " as=" << *(sl.mallocKeyOffset.mallocKey.allocSite) << " iter=" << sl.mallocKeyOffset.mallocKey.iteration << " offset=" << sl.mallocKeyOffset.offset << std::endl;
  }

  foreach(it, alist.begin(), alist.end()) {
    ArrayLocation sl = it->first;
    unsigned c = it->second;
    std::cout << "array: " << (c * 1.0 / cache.size()) << " as=" << *(sl.mallocKey.allocSite) << " iter=" << sl.mallocKey.iteration << std::endl;
  }
}

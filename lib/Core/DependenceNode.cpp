#include "DependenceNode.h"
#include "StateRecord.h"
#include "Sugar.h"
#include "Memory.h"
#include "llvm/Instructions.h"
#include <limits.h>
#include <iostream>

#include <boost/functional/hash.hpp>
#include "klee/Expr.h"
#include <iostream>
#include "klee/Internal/Module/KModule.h"
#include "llvm/Function.h"
#include "StaticRecord.h"

using namespace boost;
using namespace llvm;
using namespace klee;

////////////////////////////////////////////////////////////////////////////////
unsigned DependenceNode::inleafcount = 0;
unsigned DependenceNode::count = 0;

DependenceNode::DependenceNode(StateRecord* _rec) : lochash(0), valhash(0), rec(_rec), inst(_rec->curinst) {
  
  Function* function = rec->staticRecord->function;
  if ((function->getNameStr().find("memcpy") != std::string::npos) ||
          (function->getNameStr().find("memset") != std::string::npos) ||
          (function->getNameStr().find("strlen") != std::string::npos) ||
          (function->getNameStr().find("strcpy") != std::string::npos))
    inleafcount++;
  count++;
}

DependenceNode::~DependenceNode() {
}

void DependenceNode::print(std::ostream &out) const {
  out << "dn";
}

std::ostream & operator<<(std::ostream &out, const DependenceNode &dn) {
  dn.print(out);
  return out;
}

///////////////////////////////////////////////////////////////////////////////
unsigned ConOffArrayAlloc::count = 0;

ConOffArrayAlloc::ConOffArrayAlloc(StateRecord* allocRec, const MallocKey& mk, unsigned _offset) :
DependenceNode(allocRec), mallocKeyOffset(MallocKeyOffset(mk, _offset)) {
  count++;

  boost::hash_combine(lochash, mk.hash());
  boost::hash_combine(lochash, _offset);
  
  valhash = 1;
}

void ConOffArrayAlloc::print(std::ostream &out) const {
  out << "conoffarralloc:" << *(mallocKeyOffset.mallocKey.allocSite) << " " << mallocKeyOffset.offset;
}

///////////////////////////////////////////////////////////////////////////////
unsigned SymOffArrayAlloc::count = 0;

SymOffArrayAlloc::SymOffArrayAlloc(StateRecord* allocRec, const MallocKey& mk) :
DependenceNode(allocRec), mallocKey(mk) {
  count++;

  lochash = mallocKey.hash();
  valhash = 1;
}

void SymOffArrayAlloc::print(std::ostream &out) const {
  out << "symoffarralloc:" << *(mallocKey.allocSite);
}

///////////////////////////////////////////////////////////////////////////////
unsigned StackWrite::count = 0;

StackWrite::StackWrite(StateRecord* _rec, KFunction* _kf, unsigned _call, unsigned _sfi, unsigned _reg, ref<Expr> _value) :
DependenceNode(_rec), kf(_kf), call(_call), sfi(_sfi), reg(_reg), value(_value), hash(0) {
  count++;

  boost::hash_combine(lochash, sfi);
  boost::hash_combine(lochash, reg);

  boost::hash_combine(valhash, !value.isNull() ? value->hash() : 0);
}

void StackWrite::print(std::ostream &out) const {
  Function* f = rec->staticRecord->function;
  BasicBlock* bb = rec->staticRecord->basicBlock;
  out << "skw: " << reg << " " << *(kf->getValueForRegister(reg)) << " " << f->getNameStr() << " " << bb->getNameStr();

  if (value.get())
    out << " val=" << *value;
}

///////////////////////////////////////////////////////////////////////////////
unsigned SymOffObjectWrite::count = 0;

SymOffObjectWrite::SymOffObjectWrite(StateRecord* _rec, ObjectState* objectState) :
DependenceNode(_rec), mallocKey(objectState->getObject()->mallocKey),
objectStateCopy(new ObjectState(*objectState)) {
  count++;

  lochash = mallocKey.hash();
  valhash = 1;
}

SymOffObjectWrite::~SymOffObjectWrite() {
  delete objectStateCopy;
}

void SymOffObjectWrite::print(std::ostream &out) const {
  out << "sow:";
  if (mallocKey.allocSite) {
    out << " as=" << *(mallocKey.allocSite);
    out << ":" << mallocKey.iteration;
  }
}

///////////////////////////////////////////////////////////////////////////////
unsigned ConOffObjectWrite::count = 0;

ConOffObjectWrite::ConOffObjectWrite(StateRecord* _rec, ObjectState* os, unsigned _offset, ref<Expr> _value) :
DependenceNode(_rec), mallocKey(os->getObject()->mallocKey), offset(_offset), value(_value) {
  count++;

  boost::hash_combine(lochash, mallocKey.hash());
  boost::hash_combine(lochash, offset);

  boost::hash_combine(valhash, !value.isNull() ? value->hash() : 0);
}

void ConOffObjectWrite::print(std::ostream &out) const {
  out << "cow: rec=" << rec->staticRecord->function->getNameStr() << ":" << rec->staticRecord->basicBlock->getNameStr();
  out << " off=" << offset;

  if (value.get()) {
    out << " val=" << *value;
  }
  if (mallocKey.allocSite) {
    if (isa<Instruction > (mallocKey.allocSite)) {
      out << " as=" << *(mallocKey.allocSite);
    } else {
      out << " as=" << mallocKey.allocSite->getNameStr();
    }
  }
  out << ":" << mallocKey.iteration;

}
////////////////////////////////////////////////////////////////////////////////

bool DependenceNodeLessThan::operator() (DependenceNode* n1, DependenceNode* n2) {
  if (StackWrite * sw1 = n1->toStackWrite()) {
    if (StackWrite * sw2 = n2->toStackWrite()) {
      if (sw1->sfi < sw2->sfi) {
        return true;
      } else if ((sw1->sfi == sw2->sfi) && (sw1->reg < sw2->reg)) {
        return true;
      } else {
        return false;
      }
    } else {
      return true;
    }
  }
  if (ConOffObjectWrite * cw1 = n1->toConOffObjectWrite()) {
    if (n2->toStackWrite()) {
      return false;
    } else if (ConOffObjectWrite * cw2 = n2->toConOffObjectWrite()) {
      if (cw1->mallocKey < cw2->mallocKey) {
        return true;
      } else if ((cw1->mallocKey == cw2->mallocKey) && (cw1->offset < cw2->offset)) {
        return true;
      } else {
        return false;
      }
    } else {
      return true;
    }
  }
  if (SymOffObjectWrite * sw1 = n1->toSymOffObjectWrite()) {
    if (n2->toStackWrite()) {
      return false;
    } else if (n2->toConOffObjectWrite()) {
      return false;
    } else if (SymOffObjectWrite * sw2 = n2->toSymOffObjectWrite()) {
      return sw1->mallocKey < sw2->mallocKey;
    } else {
      return true;
    }
  }
  if (ConOffArrayAlloc * arr1 = n1->toConOffArrayAlloc()) {
    if (n2->toStackWrite()) {
      return false;
    } else if (n2->toConOffObjectWrite()) {
      return false;
    } else if (n2->toSymOffObjectWrite()) {
      return false;
    } else if (ConOffArrayAlloc * arr2 = n2->toConOffArrayAlloc()) {
      return arr1->mallocKeyOffset < arr2->mallocKeyOffset;
    } else {
      return true;
    }
  }
  if (SymOffArrayAlloc * arr1 = n1->toSymOffArrayAlloc()) {
    if (n2->toStackWrite()) {
      return false;
    } else if (n2->toConOffObjectWrite()) {
      return false;
    } else if (n2->toSymOffObjectWrite()) {
      return false;
    } else if (n2->toConOffArrayAlloc()) {
      return false;
    } else if (SymOffArrayAlloc * arr2 = n2->toSymOffArrayAlloc()) {
      return arr1->mallocKey < arr2->mallocKey;
    } else {
      assert(false);
    }
  }

  assert(false);
}
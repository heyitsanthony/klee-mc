//===-- Expr.cpp ----------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include <stdio.h>
#include "klee/Expr.h"

#include "llvm/Support/CommandLine.h"
// FIXME: We shouldn't need this once fast constant support moves into
// Core. If we need to do arithmetic, we probably want to use APInt.
#include "klee/Internal/Support/IntEvaluation.h"

#include "klee/util/ExprPPrinter.h"

#include <iostream>
#include <sstream>

using namespace klee;
using namespace llvm;

namespace {
  cl::opt<bool>
  ConstArrayOpt("const-array-opt",
	 cl::init(false),
	 cl::desc("Enable various optimizations involving all-constant arrays."));
}

/***/

unsigned Expr::count = 0;

bool ArrayLT::operator()(const Array *a, const Array *b) const
{ return *a < *b; }

ref<Expr> Expr::createTempRead(const Array *array, Expr::Width w) {
  UpdateList ul(array, 0);

  switch (w) {
  default: assert(0 && "invalid width");
  case Expr::Bool:
    return ZExtExpr::create(ReadExpr::create(ul,
                                             ConstantExpr::alloc(0, Expr::Int32)),
                            Expr::Bool);
  case Expr::Int8:
    return ReadExpr::create(ul,
                            ConstantExpr::alloc(0,Expr::Int32));
  case Expr::Int16:
    return ConcatExpr::create(ReadExpr::create(ul,
                                               ConstantExpr::alloc(1,Expr::Int32)),
                              ReadExpr::create(ul,
                                               ConstantExpr::alloc(0,Expr::Int32)));
  case Expr::Int32:
    return ConcatExpr::create4(ReadExpr::create(ul,
                                                ConstantExpr::alloc(3,Expr::Int32)),
                               ReadExpr::create(ul,
                                                ConstantExpr::alloc(2,Expr::Int32)),
                               ReadExpr::create(ul,
                                                ConstantExpr::alloc(1,Expr::Int32)),
                               ReadExpr::create(ul,
                                                ConstantExpr::alloc(0,Expr::Int32)));
  case Expr::Int64:
    return ConcatExpr::create8(ReadExpr::create(ul,
                                                ConstantExpr::alloc(7,Expr::Int32)),
                               ReadExpr::create(ul,
                                                ConstantExpr::alloc(6,Expr::Int32)),
                               ReadExpr::create(ul,
                                                ConstantExpr::alloc(5,Expr::Int32)),
                               ReadExpr::create(ul,
                                                ConstantExpr::alloc(4,Expr::Int32)),
                               ReadExpr::create(ul,
                                                ConstantExpr::alloc(3,Expr::Int32)),
                               ReadExpr::create(ul,
                                                ConstantExpr::alloc(2,Expr::Int32)),
                               ReadExpr::create(ul,
                                                ConstantExpr::alloc(1,Expr::Int32)),
                               ReadExpr::create(ul,
                                                ConstantExpr::alloc(0,Expr::Int32)));
  }
}

/* Slow path for comparison. This should only be used by Expr::compare */
int Expr::compareSlow(const Expr& b) const
{
	Kind ak = getKind(), bk = b.getKind();
	if (ak!=bk)
		return (ak < bk) ? -1 : 1;

	if (hashValue != b.hashValue/* && !isa<ConstantExpr>(*this)*/)
		return (hashValue < b.hashValue) ? -1 : 1;

	if (int res = compareContents(b))
		return res;

	unsigned aN = getNumKids();
	for (unsigned i=0; i<aN; i++)
		if (int res = getKid(i).compare(b.getKid(i)))
			return res;

	return 0;
}

void Expr::printKind(std::ostream &os, Kind k) {
  switch(k) {
#define X(C) case C: os << #C; break
    X(Constant);
    X(NotOptimized);
    X(Read);
    X(Select);
    X(Concat);
    X(Extract);
    X(ZExt);
    X(SExt);
    X(Add);
    X(Sub);
    X(Mul);
    X(UDiv);
    X(SDiv);
    X(URem);
    X(SRem);
    X(Not);
    X(And);
    X(Or);
    X(Xor);
    X(Shl);
    X(LShr);
    X(AShr);
    X(Eq);
    X(Ne);
    X(Ult);
    X(Ule);
    X(Ugt);
    X(Uge);
    X(Slt);
    X(Sle);
    X(Sgt);
    X(Sge);
#undef X
  default:
    assert(0 && "invalid kind");
    }
}

////////
//
// Simple hash functions for various kinds of Exprs
//
///////

unsigned Expr::computeHash() {
  unsigned res = getKind() * Expr::MAGIC_HASH_CONSTANT;

  int n = getNumKids();
  for (int i = 0; i < n; i++) {
    res <<= 1;
    res ^= getKid(i)->hash() * Expr::MAGIC_HASH_CONSTANT;
  }

  hashValue = res;
  return hashValue;
}

unsigned ConstantExpr::computeHash() {
  hashValue = value.getHashValue() ^ (getWidth() * MAGIC_HASH_CONSTANT);
  return hashValue;
}

unsigned CastExpr::computeHash() {
  unsigned res = getWidth() * Expr::MAGIC_HASH_CONSTANT;
  hashValue = res ^ src->hash() * Expr::MAGIC_HASH_CONSTANT;
  return hashValue;
}

unsigned ExtractExpr::computeHash() {
  unsigned res = offset * Expr::MAGIC_HASH_CONSTANT;
  res ^= getWidth() * Expr::MAGIC_HASH_CONSTANT;
  hashValue = res ^ expr->hash() * Expr::MAGIC_HASH_CONSTANT;
  return hashValue;
}

unsigned ReadExpr::computeHash() {
  unsigned h_idx = index->hash();
  unsigned h_updates = updates.hash();
  unsigned res = (h_idx * Expr::MAGIC_HASH_CONSTANT);
  res ^= h_updates;
  hashValue = res;
  return hashValue;
}

unsigned NotExpr::computeHash() {
  unsigned hashValue = expr->hash() * Expr::MAGIC_HASH_CONSTANT * Expr::Not;
  return hashValue;
}

ref<Expr> Expr::createFromKind(Kind k, std::vector<CreateArg> args) {
  unsigned numArgs = args.size();
  (void) numArgs;

  switch(k) {
    case Constant:
    case Extract:
    case Read:
    default:
      assert(0 && "invalid kind");

    case NotOptimized:
      assert(numArgs == 1 && args[0].isExpr() &&
             "invalid args array for given opcode");
      return NotOptimizedExpr::create(args[0].expr);

    case Select:
      assert(numArgs == 3 && args[0].isExpr() &&
             args[1].isExpr() && args[2].isExpr() &&
             "invalid args array for Select opcode");
      return SelectExpr::create(args[0].expr,
                                args[1].expr,
                                args[2].expr);

    case Concat: {
      assert(numArgs == 2 && args[0].isExpr() && args[1].isExpr() &&
             "invalid args array for Concat opcode");

      return ConcatExpr::create(args[0].expr, args[1].expr);
    }

#define CAST_EXPR_CASE(T)                                    \
      case T:                                                \
        assert(numArgs == 2 &&				     \
               args[0].isExpr() && args[1].isWidth() &&      \
               "invalid args array for given opcode");       \
      return T ## Expr::create(args[0].expr, args[1].width); \

      CAST_EXPR_CASE(ZExt);
      CAST_EXPR_CASE(SExt);

      case Add:
      case Sub:
      case Mul:
      case UDiv:
      case SDiv:
      case URem:
      case SRem:
      case And:
      case Or:
      case Xor:
      case Shl:
      case LShr:
      case AShr:
      case Eq:
      case Ne:
      case Ult:
      case Ule:
      case Ugt:
      case Uge:
      case Slt:
      case Sle:
      case Sgt:
      case Sge:
        assert (numArgs == 2 &&
		args[0].isExpr() && args[1].isExpr() && "invalid args array");
        return BinaryExpr::create(k, args[0].expr, args[1].expr);
  }
}


void Expr::printWidth(std::ostream &os, Width width) {
  switch(width) {
  case Expr::Bool: os << "Expr::Bool"; break;
  case Expr::Int8: os << "Expr::Int8"; break;
  case Expr::Int16: os << "Expr::Int16"; break;
  case Expr::Int32: os << "Expr::Int32"; break;
  case Expr::Int64: os << "Expr::Int64"; break;
  case Expr::Fl80: os << "Expr::Fl80"; break;
  default: os << "<invalid type: " << (unsigned) width << ">";
  }
}

ref<Expr> Expr::createImplies(ref<Expr> hyp, ref<Expr> conc) {
  return OrExpr::create(Expr::createIsZero(hyp), conc);
}

ref<Expr> Expr::createIsZero(ref<Expr> e) {
  return EqExpr::create(e, ConstantExpr::create(0, e->getWidth()));
}

void Expr::print(std::ostream &os) const
{
	ref<Expr>	e(const_cast<Expr*>(this));
	ExprPPrinter::printSingleExpr(os, e);
}

void Expr::dump() const {
  this->print(std::cerr);
  std::cerr << std::endl;
}

ref<Expr>  NotOptimizedExpr::create(ref<Expr> src) {
  return NotOptimizedExpr::alloc(src);
}
/***/

MallocKey::seensizes_ty MallocKey::seenSizes;

// Compare two MallocKeys
// Returns:
//  0 if allocSite and iteration match and size >= a.size and size <= a.size's
//    lower bound
//  -1 if allocSite or iteration do not match and operator< returns true or
//     allocSite and iteration match but size < a.size
//  1  if allocSite or iteration do not match and operator< returns false or
//     allocSite and iteration match but size > a.size's lower bound
int MallocKey::compare(const MallocKey &a) const {
  if (allocSite == a.allocSite && iteration == a.iteration) {
    if (size < a.size)
      return -1;
    else if (size == a.size)
      return 0;
    else { // size > a.size; check whether they share a lower bound
      std::set<uint64_t>::iterator it = seenSizes[*this].lower_bound(a.size);
      assert(it != seenSizes[*this].end());
      if (size <= *it)
        return 0;
      else // this->size > lower bound, so *this > a
        return 1;
    }
  } else
    return (*this < a) ? -1 : 1;
}

#include "llvm/BasicBlock.h"
#include "llvm/Function.h"
#include "llvm/Instruction.h"
#include "llvm/Value.h"


unsigned MallocKey::hash() const
{
  unsigned	res = 0;
  unsigned long	alloc_v;

  const Instruction	*ins;
  ins = dyn_cast<Instruction>(allocSite);
  if (ins == NULL) {
  	alloc_v = 1;
  } else {
  	/* before we were using the pointer value from allocSite.
	 * this broke stuff horribly, so use bb+func names. */
	std::string	s_bb, s_f;
	s_bb = ins->getParent()->getNameStr();
	s_f = ins->getParent()->getParent()->getNameStr();
	alloc_v = 0;
	for (unsigned int i = 0; i < s_bb.size(); i++)
		alloc_v = alloc_v*33+s_bb[i];
	for (unsigned int i = 0; i < s_f.size(); i++)
		alloc_v = alloc_v*33+s_f[i];
  }

  res = alloc_v * Expr::MAGIC_HASH_CONSTANT;

  res ^= iteration * Expr::MAGIC_HASH_CONSTANT;
  return res;
}

/***/

ref<Expr> ReadExpr::create(const UpdateList &ul, ref<Expr> index)
{
  // rollback index when possible...

  // sanity check for OoB read
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(index)) {
    assert(CE->getZExtValue() < ul.root->mallocKey.size);
  }

  // XXX this doesn't really belong here... there are basically two
  // cases, one is rebuild, where we want to optimistically try various
  // optimizations when the index has changed, and the other is
  // initial creation, where we expect the ObjectState to have constructed
  // a smart UpdateList so it is not worth rescanning.

  const UpdateNode *un = ul.head;
  for (; un; un=un->next) {
    ref<Expr> cond = EqExpr::create(index, un->index);

    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(cond)) {
      if (CE->isTrue())
        return un->value;
    } else {
      break;
    }
  }

  return ReadExpr::alloc(ul, index);
}

int ReadExpr::compareContents(const Expr &b) const {
  return updates.compare(static_cast<const ReadExpr&>(b).updates);
}

ref<Expr> SelectExpr::create(ref<Expr> c, ref<Expr> t, ref<Expr> f) {
  Expr::Width kt = t->getWidth();

  assert(c->getWidth()==Bool && "type mismatch");
  assert(kt==f->getWidth() && "type mismatch");

  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(c)) {
    return CE->isTrue() ? t : f;
  } else if (t==f) {
    return t;
  } else if (kt==Expr::Bool) { // c ? t : f  <=> (c and t) or (not c and f)
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(t)) {
      if (CE->isTrue()) {
        return OrExpr::create(c, f);
      } else {
        return AndExpr::create(Expr::createIsZero(c), f);
      }
    } else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(f)) {
      if (CE->isTrue()) {
        return OrExpr::create(Expr::createIsZero(c), t);
      } else {
        return AndExpr::create(c, t);
      }
    }
  }

  return SelectExpr::alloc(c, t, f);
}

/***/

ref<Expr> ConcatExpr::create(const ref<Expr> &l, const ref<Expr> &r)
{
	Expr::Width w = l->getWidth() + r->getWidth();

	// Fold concatenation of constants.
	//
	// FIXME: concat 0 x -> zext x ?
	if (ConstantExpr *lCE = dyn_cast<ConstantExpr>(l)) {
		if (ConstantExpr *rCE = dyn_cast<ConstantExpr>(r))
			return lCE->Concat(rCE);
		else if (ConcatExpr *ce_right = dyn_cast<ConcatExpr>(r)) {
			ConstantExpr *rCE;
			rCE = dyn_cast<ConstantExpr>(ce_right->left);
			if (rCE)
				return ConcatExpr::create(
					lCE->Concat(rCE), ce_right->right);
		}
	}

	// Merge contiguous Extracts
	ExtractExpr	*ee_left;

	ee_left = dyn_cast<ExtractExpr>(l);
	if (ee_left) {
		ExtractExpr *ee_right;
		ee_right = dyn_cast<ExtractExpr>(r);
		if (ee_right) {
			if (	ee_left->expr == ee_right->expr &&
				ee_right->offset + ee_right->width == ee_left->offset)
			{
				return ExtractExpr::create(
					ee_left->expr, ee_right->offset, w);
			}
		}
		// concat(extract(x[j+1]), concat(extract(x[j]), ...)
		//   => concat(extract(x[j+1:j]), ...)
		else if (ConcatExpr *ce_right = dyn_cast<ConcatExpr>(r)) {
			ee_right = dyn_cast<ExtractExpr>(ce_right->left);
			if (	ee_right &&
				ee_left->expr == ee_right->expr &&
				ee_left->offset == ee_right->offset + ee_right->width)
			{
				return ConcatExpr::create(
					ExtractExpr::create(
						ee_left->expr,
						ee_right->offset,
						ee_left->width+ee_right->width),
					ce_right->right);
			}
		}

	}

	return ConcatExpr::alloc(l, r);
}

/// Shortcut to concat N kids.  The chain returned is unbalanced to the right
ref<Expr> ConcatExpr::createN(unsigned n_kids, const ref<Expr> kids[]) {
  assert(n_kids > 0);
  if (n_kids == 1)
    return kids[0];

  ref<Expr> r = ConcatExpr::create(kids[n_kids-2], kids[n_kids-1]);
  for (int i=n_kids-3; i>=0; i--)
    r = ConcatExpr::create(kids[i], r);
  return r;
}

/// Shortcut to concat 4 kids.  The chain returned is unbalanced to the right
ref<Expr> ConcatExpr::create4(const ref<Expr> &kid1, const ref<Expr> &kid2,
                              const ref<Expr> &kid3, const ref<Expr> &kid4) {
  return ConcatExpr::create(kid1, ConcatExpr::create(kid2, ConcatExpr::create(kid3, kid4)));
}

/// Shortcut to concat 8 kids.  The chain returned is unbalanced to the right
ref<Expr> ConcatExpr::create8(const ref<Expr> &kid1, const ref<Expr> &kid2,
			      const ref<Expr> &kid3, const ref<Expr> &kid4,
			      const ref<Expr> &kid5, const ref<Expr> &kid6,
			      const ref<Expr> &kid7, const ref<Expr> &kid8) {
  return ConcatExpr::create(kid1, ConcatExpr::create(kid2, ConcatExpr::create(kid3,
			      ConcatExpr::create(kid4, ConcatExpr::create4(kid5, kid6, kid7, kid8)))));
}

/***/

ref<Expr> ExtractExpr::create(ref<Expr> expr, unsigned off, Width w)
{
	unsigned kw = expr->getWidth();
	assert(w > 0 && off + w <= kw && "invalid extract");

	if (w == kw)
		return expr;

	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr))
		return CE->Extract(off, w);

	// Extract(Concat)
	if (ConcatExpr *ce = dyn_cast<ConcatExpr>(expr)) {
		// if the extract skips the right side of the concat
		if (off >= ce->getRight()->getWidth())
			return ExtractExpr::create(
				ce->getLeft(),
				off - ce->getRight()->getWidth(), w);

		// if the extract skips the left side of the concat
		if (off + w <= ce->getRight()->getWidth())
			return ExtractExpr::create(ce->getRight(), off, w);

		// E(C(x,y)) = C(E(x), E(y))
		// XXX is this wise? it creates more expressions..
		return ConcatExpr::create(
			ExtractExpr::create(
				ce->getKid(0),
				0, w - ce->getKid(1)->getWidth() + off),
			ExtractExpr::create(
				ce->getKid(1),
				off, ce->getKid(1)->getWidth() - off));
	}

	if (off == 0) {
		// Extract(0,{Z,S}Ext(x)) = x
		if (CastExpr *ce = dyn_cast<CastExpr>(expr)) {
			if (ce->src->getWidth() >= w) {
				return ExtractExpr::create(ce->src, off, w);
			}
		} else  if (const ZExtExpr* ze = dyn_cast<ZExtExpr>(expr)) {
			// qemu gave me this gem:
			// extract[31:0] ( zero_extend[56] (select w8) )
			return ZExtExpr::create(ze->src, w);
		} else if (BinaryExpr *be = dyn_cast<BinaryExpr>(expr)) {
			Kind rk = be->getKind();
			// E(x + y) = E(x) + E(y)
			if (	rk == Add || rk == Sub ||
				rk == And || rk == Or ||
				rk == Mul)
			{
				return BinaryExpr::create(
					rk,
					ExtractExpr::create(be->left, off, w),
					ExtractExpr::create(be->right, off, w));
			}
		}
	}

	/* Extract(Extract) */
	if (const ExtractExpr* ee = dyn_cast<ExtractExpr>(expr)) {
		return ExtractExpr::alloc(ee->expr, off+ee->offset, w);
	}

#if 0
	if (const SelectExpr *se = dyn_cast<SelectExpr>(expr)) {
		std::vector<ref<Expr> > values(se->values.size());
		for (unsigned i = 0; i < se->values.size(); i++)
			values[i] = ExtractExpr::create(se->values[i], off, w);
		return SelectExpr::create(se->conds, values);
	}
#endif

	return ExtractExpr::alloc(expr, off, w);
}

/***/

ref<Expr> NotExpr::create(const ref<Expr> &e) {
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(e))
    return CE->Not();

  return NotExpr::alloc(e);
}

/***/

ref<Expr> BinaryExpr::create(Kind k, const ref<Expr> &l, const ref<Expr> &r) {
#define BINARY_EXPR_CASE(T) \
	case T: return T ## Expr::create(l, r);

	switch (k) {
	default:
		assert(0 && "invalid kind");

	BINARY_EXPR_CASE(Add);
	BINARY_EXPR_CASE(Sub);
	BINARY_EXPR_CASE(Mul);
	BINARY_EXPR_CASE(UDiv);
	BINARY_EXPR_CASE(SDiv);
	BINARY_EXPR_CASE(URem);
	BINARY_EXPR_CASE(SRem);
	BINARY_EXPR_CASE(And);
	BINARY_EXPR_CASE(Or);
	BINARY_EXPR_CASE(Xor);
	BINARY_EXPR_CASE(Shl);
	BINARY_EXPR_CASE(LShr);
	BINARY_EXPR_CASE(AShr);

	BINARY_EXPR_CASE(Eq);
	BINARY_EXPR_CASE(Ne);
	BINARY_EXPR_CASE(Ult);
	BINARY_EXPR_CASE(Ule);
	BINARY_EXPR_CASE(Ugt);
	BINARY_EXPR_CASE(Uge);
	BINARY_EXPR_CASE(Slt);
	BINARY_EXPR_CASE(Sle);
	BINARY_EXPR_CASE(Sgt);
	BINARY_EXPR_CASE(Sge);
	}
#undef BINARY_EXPR_CASE
}

/***/

ref<Expr> ZExtExpr::create(const ref<Expr> &e, Width w)
{
	unsigned kBits = e->getWidth();

	if (w == kBits) return e;

	// trunc
	if (w < kBits) return ExtractExpr::create(e, 0, w);

	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(e))
		return CE->ZExt(w);

#if 0
	/* In David's branch this only happens if 'optimize' is set;
	 * it's not clear to me reason why you not want to do this. */
	if (const SelectExpr *se = dyn_cast<SelectExpr>(e)) {
		std::vector<ref<Expr> > values(se->values.size());
		for (unsigned i = 0; i < se->values.size(); i++)
			values[i] = ZExtExpr::create(se->values[i], w);
		return SelectExpr::create(se->conds, values);
	}
#endif
	if (kBits == 1) {
		return SelectExpr::create(
			e,
			ConstantExpr::alloc(1, w),
			ConstantExpr::alloc(0, w));
	}

	// there are optimizations elsewhere that deal with concatenations of
	// constants within their arguments, so we're better off concatenating 0
	// than using ZExt. ZExt(X, w) = Concat(0, X)
	//
	// But for now, don't do it.
	// return ConcatExpr::create(ConstantExpr::alloc(0, w - kBits), e);

	return ZExtExpr::alloc(e, w);
}

ref<Expr> SExtExpr::create(const ref<Expr> &e, Width w) {
  unsigned kBits = e->getWidth();
  if (w == kBits) {
    return e;
  } else if (w < kBits) { // trunc
    return ExtractExpr::create(e, 0, w);
  } else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(e)) {
    return CE->SExt(w);
  } else {
    return SExtExpr::alloc(e, w);
  }
}

/***/

static ref<Expr> AndExpr_create(Expr *l, Expr *r);
static ref<Expr> XorExpr_create(Expr *l, Expr *r);

static ref<Expr> EqExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr);
static ref<Expr> AndExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r);
static ref<Expr> SubExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r);
static ref<Expr> XorExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r);

static ref<Expr> AddExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r) {
  Expr::Width type = cl->getWidth();

  if (type==Expr::Bool) {
    return XorExpr_createPartialR(cl, r);
  } else if (cl->isZero()) {
    return r;
  } else {
    Expr::Kind rk = r->getKind();
    if (rk==Expr::Add && isa<ConstantExpr>(r->getKid(0))) { // A + (B+c) == (A+B) + c
      return AddExpr::create(AddExpr::create(cl, r->getKid(0)),
                             r->getKid(1));
    } else if (rk==Expr::Sub && isa<ConstantExpr>(r->getKid(0))) { // A + (B-c) == (A+B) - c
      return SubExpr::create(AddExpr::create(cl, r->getKid(0)),
                             r->getKid(1));
    } else {
      return AddExpr::alloc(cl, r);
    }
  }
}
static ref<Expr> AddExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr) {
  return AddExpr_createPartialR(cr, l);
}
static ref<Expr> AddExpr_create(Expr *l, Expr *r) {
  Expr::Width type = l->getWidth();

  if (type == Expr::Bool) {
    return XorExpr_create(l, r);
  } else {
    Expr::Kind lk = l->getKind(), rk = r->getKind();
    if (lk==Expr::Add && isa<ConstantExpr>(l->getKid(0))) { // (k+a)+b = k+(a+b)
      return AddExpr::create(l->getKid(0),
                             AddExpr::create(l->getKid(1), r));
    } else if (lk==Expr::Sub && isa<ConstantExpr>(l->getKid(0))) { // (k-a)+b = k+(b-a)
      return AddExpr::create(l->getKid(0),
                             SubExpr::create(r, l->getKid(1)));
    } else if (rk==Expr::Add && isa<ConstantExpr>(r->getKid(0))) { // a + (k+b) = k+(a+b)
      return AddExpr::create(r->getKid(0),
                             AddExpr::create(l, r->getKid(1)));
    } else if (rk==Expr::Sub && isa<ConstantExpr>(r->getKid(0))) { // a + (k-b) = k+(a-b)
      return AddExpr::create(r->getKid(0),
                             SubExpr::create(l, r->getKid(1)));
    } else {
      return AddExpr::alloc(l, r);
    }
  }
}

static ref<Expr> SubExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r) {
  Expr::Width type = cl->getWidth();

  if (type==Expr::Bool) {
    return XorExpr_createPartialR(cl, r);
  } else {
    Expr::Kind rk = r->getKind();
    if (rk==Expr::Add && isa<ConstantExpr>(r->getKid(0))) { // A - (B+c) == (A-B) - c
      return SubExpr::create(SubExpr::create(cl, r->getKid(0)),
                             r->getKid(1));
    } else if (rk==Expr::Sub && isa<ConstantExpr>(r->getKid(0))) { // A - (B-c) == (A-B) + c
      return AddExpr::create(SubExpr::create(cl, r->getKid(0)),
                             r->getKid(1));
    } else {
      return SubExpr::alloc(cl, r);
    }
  }
}
static ref<Expr> SubExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr) {
  // l - c => l + (-c)
  return AddExpr_createPartial(l,
                               ConstantExpr::alloc(0, cr->getWidth())->Sub(cr));
}
static ref<Expr> SubExpr_create(Expr *l, Expr *r) {
  Expr::Width type = l->getWidth();

  if (type == Expr::Bool) {
    return XorExpr_create(l, r);
  } else if (*l==*r) {
    return ConstantExpr::alloc(0, type);
  } else {
    Expr::Kind lk = l->getKind(), rk = r->getKind();
    if (lk==Expr::Add && isa<ConstantExpr>(l->getKid(0))) { // (k+a)-b = k+(a-b)
      return AddExpr::create(l->getKid(0),
                             SubExpr::create(l->getKid(1), r));
    } else if (lk==Expr::Sub && isa<ConstantExpr>(l->getKid(0))) { // (k-a)-b = k-(a+b)
      return SubExpr::create(l->getKid(0),
                             AddExpr::create(l->getKid(1), r));
    } else if (rk==Expr::Add && isa<ConstantExpr>(r->getKid(0))) { // a - (k+b) = (a-c) - k
      return SubExpr::create(SubExpr::create(l, r->getKid(1)),
                             r->getKid(0));
    } else if (rk==Expr::Sub && isa<ConstantExpr>(r->getKid(0))) { // a - (k-b) = (a+b) - k
      return SubExpr::create(AddExpr::create(l, r->getKid(1)),
                             r->getKid(0));
    } else {
      return SubExpr::alloc(l, r);
    }
  }
}

static ref<Expr> MulExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r) {
  Expr::Width type = cl->getWidth();

  if (type == Expr::Bool) {
    return AndExpr_createPartialR(cl, r);
  } else if (cl->isOne()) {
    return r;
  } else if (cl->isZero()) {
    return cl;
  } else {
    return MulExpr::alloc(cl, r);
  }
}
static ref<Expr> MulExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr) {
  return MulExpr_createPartialR(cr, l);
}
static ref<Expr> MulExpr_create(Expr *l, Expr *r) {
  Expr::Width type = l->getWidth();

  if (type == Expr::Bool) {
    return AndExpr::alloc(l, r);
  } else {
    return MulExpr::alloc(l, r);
  }
}

static ref<Expr> AndExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr)
{
	if (cr->isAllOnes()) return l;
	if (cr->isZero()) return cr;

	if (ConcatExpr *cl = dyn_cast<ConcatExpr>(l)) {
		// AND(	Concat(x,y), N) <==>
		// 	Concat(And(x, N[...]), And(y, N[...:0]))
		return ConcatExpr::create(
			AndExpr_createPartial(
				cl->getLeft().get(),
				cr->Extract(
					cl->getRight()->getWidth(),
					cl->getLeft()->getWidth())),
			AndExpr_createPartial(
				cl->getRight().get(),
				cr->Extract(
					0,
					cl->getRight()->getWidth())));
	}

	return AndExpr::alloc(l, cr);
}
static ref<Expr> AndExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r) {
  return AndExpr_createPartial(r, cl);
}
static ref<Expr> AndExpr_create(Expr *l, Expr *r)
{
	if (*l == *r)
		return l;

	if (l->getWidth() == Expr::Bool) {
		// a && !a = false
		if (*l == *Expr::createIsZero(r).get())
			return ConstantExpr::create(0, Expr::Bool);
	}

	CmpExpr *ce_left, *ce_right;
	ce_left = dyn_cast<CmpExpr>(l);
	ce_right = dyn_cast<CmpExpr>(r);
	if (ce_left && ce_right) {
		// (x <= y) & (y <= x) <==> x == y
		if (	((isa<UleExpr>(ce_left) && isa<UleExpr>(ce_right))
			|| (isa<SleExpr>(ce_left) && isa<SleExpr>(ce_right)))
			&& ce_left->left == ce_right->right
			&& ce_left->right == ce_right->left)
		{
			return EqExpr::create(ce_left->left, ce_left->right);
		}

		// (x < y) & (y < x) <==> false
		if (	((isa<UltExpr>(ce_left) && isa<UltExpr>(ce_right))
			|| (isa<SltExpr>(ce_left) && isa<SltExpr>(ce_right)))
			&& ce_left->left == ce_right->right
			&& ce_left->right == ce_right->left)
		{
			return ConstantExpr::create(0, Expr::Bool);
		}
	}

	return AndExpr::alloc(l, r);
}

static ref<Expr> OrExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr) {
  if (cr->isAllOnes()) {
    return cr;
  } else if (cr->isZero()) {
    return l;
  } else {
    return OrExpr::alloc(l, cr);
  }
}
static ref<Expr> OrExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r) {
  return OrExpr_createPartial(r, cl);
}
static ref<Expr> OrExpr_create(Expr *l, Expr *r) {
  if (*l == *r)
    return l;
  else if (l->getWidth() == Expr::Bool) {
    if (*l == *Expr::createIsZero(r).get()) // a || !a = true
      return ConstantExpr::create(1, Expr::Bool);
  }
  return OrExpr::alloc(l, r);
}

static ref<Expr> XorExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r) {
  if (cl->isZero()) {
    return r;
  } else if (cl->getWidth() == Expr::Bool) {
    return EqExpr_createPartial(r, ConstantExpr::create(0, Expr::Bool));
  } else {
    return XorExpr::alloc(cl, r);
  }
}

static ref<Expr> XorExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr) {
  return XorExpr_createPartialR(cr, l);
}
static ref<Expr> XorExpr_create(Expr *l, Expr *r) {
  return XorExpr::alloc(l, r);
}

static ref<Expr> UDivExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l->getWidth() == Expr::Bool) { // r must be 1
    return l;
  } else{
    return UDivExpr::alloc(l, r);
  }
}

static ref<Expr> SDivExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l->getWidth() == Expr::Bool) { // r must be 1
    return l;
  } else{
    return SDivExpr::alloc(l, r);
  }
}

static ref<Expr> URemExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l->getWidth() == Expr::Bool) { // r must be 1
    return ConstantExpr::create(0, Expr::Bool);
  } else{
    if (l->isZero()) // special case: 0 % x = 0
      return l;
    else
      return URemExpr::alloc(l, r);
  }
}

static ref<Expr> SRemExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l->getWidth() == Expr::Bool) { // r must be 1
    return ConstantExpr::create(0, Expr::Bool);
  } else{
    return SRemExpr::alloc(l, r);
  }
}

static ref<Expr> ShlExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l->getWidth() == Expr::Bool) { // l & !r
    return AndExpr::create(l, Expr::createIsZero(r));
  } else{
    return ShlExpr::alloc(l, r);
  }
}

static ref<Expr> LShrExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l->getWidth() == Expr::Bool) { // l & !r
    return AndExpr::create(l, Expr::createIsZero(r));
  } else{
    return LShrExpr::alloc(l, r);
  }
}

static ref<Expr> AShrExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l->getWidth() == Expr::Bool) { // l
    return l;
  } else{
    return AShrExpr::alloc(l, r);
  }
}

#define BCREATE_R(_e_op, _op, partialL, partialR) \
ref<Expr>  _e_op ::create(const ref<Expr> &l, const ref<Expr> &r) {	\
	assert(l->getWidth()==r->getWidth() && "type mismatch");	\
	if (ConstantExpr *cl = dyn_cast<ConstantExpr>(l)) {		\
		if (ConstantExpr *cr = dyn_cast<ConstantExpr>(r))	\
			return cl->_op(cr);				\
		return _e_op ## _createPartialR(cl, r.get());		\
	} 								\
	if (ConstantExpr *cr = dyn_cast<ConstantExpr>(r)) {		\
		return _e_op ## _createPartial(l.get(), cr);		\
	}								\
	return _e_op ## _create(l.get(), r.get());			\
}

#define BCREATE(_e_op, _op) \
ref<Expr>  _e_op ::create(const ref<Expr> &l, const ref<Expr> &r) { \
  assert(l->getWidth()==r->getWidth() && "type mismatch");          \
  if (ConstantExpr *cl = dyn_cast<ConstantExpr>(l))                 \
    if (ConstantExpr *cr = dyn_cast<ConstantExpr>(r))               \
      return cl->_op(cr);                                           \
  return _e_op ## _create(l, r);                                    \
}

BCREATE_R(AddExpr, Add, AddExpr_createPartial, AddExpr_createPartialR)
BCREATE_R(SubExpr, Sub, SubExpr_createPartial, SubExpr_createPartialR)
BCREATE_R(MulExpr, Mul, MulExpr_createPartial, MulExpr_createPartialR)
BCREATE_R(AndExpr, And, AndExpr_createPartial, AndExpr_createPartialR)
BCREATE_R(OrExpr, Or, OrExpr_createPartial, OrExpr_createPartialR)
BCREATE_R(XorExpr, Xor, XorExpr_createPartial, XorExpr_createPartialR)
BCREATE(UDivExpr, UDiv)
BCREATE(SDivExpr, SDiv)
BCREATE(URemExpr, URem)
BCREATE(SRemExpr, SRem)
BCREATE(ShlExpr, Shl)
BCREATE(LShrExpr, LShr)
BCREATE(AShrExpr, AShr)

#define CMPCREATE(_e_op, _op) \
ref<Expr>  _e_op ::create(const ref<Expr> &l, const ref<Expr> &r) {	\
  assert(l->getWidth()==r->getWidth() && "type mismatch");              \
  if (ConstantExpr *cl = dyn_cast<ConstantExpr>(l))                     \
    if (ConstantExpr *cr = dyn_cast<ConstantExpr>(r)) {                 \
      return cl->_op(cr);                                               \
     }									\
  return _e_op ## _create(l, r);                                        \
}

#define CMPCREATE_T(_e_op, _op, _reflexive_e_op, partialL, partialR) \
ref<Expr>  _e_op ::create(const ref<Expr> &l, const ref<Expr> &r) {    \
  assert(l->getWidth()==r->getWidth() && "type mismatch");             \
  if (ConstantExpr *cl = dyn_cast<ConstantExpr>(l)) {                  \
    if (ConstantExpr *cr = dyn_cast<ConstantExpr>(r))                  \
      return cl->_op(cr);                                              \
    return partialR(cl, r.get());                                      \
  } else if (ConstantExpr *cr = dyn_cast<ConstantExpr>(r)) {           \
    return partialL(l.get(), cr);                                      \
  } else {                                                             \
    return _e_op ## _create(l.get(), r.get());                         \
  }                                                                    \
}


static ref<Expr> EqExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l == r) {
    return ConstantExpr::alloc(1, Expr::Bool);
  } else {
    return EqExpr::alloc(l, r);
  }
}


/// Tries to optimize EqExpr cl == rd, where cl is a ConstantExpr and
/// rd a ReadExpr.  If rd is a read into an all-constant array,
/// returns a disjunction of equalities on the index.  Otherwise,
/// returns the initial equality expression.
static ref<Expr> TryConstArrayOpt(const ref<ConstantExpr> &cl,
				  ReadExpr *rd) {
  if (rd->updates.root->isSymbolicArray() || rd->updates.getSize())
    return EqExpr_create(cl, rd);

  // Number of positions in the array that contain value ct.
  unsigned numMatches = 0;

  // for now, just assume standard "flushing" of a concrete array,
  // where the concrete array has one update for each index, in order
  ref<Expr> res = ConstantExpr::alloc(0, Expr::Bool);
  for (unsigned i = 0, e = rd->updates.root->mallocKey.size; i != e; ++i) {
    if (cl == rd->updates.root->getValue(i)) {
      // Arbitrary maximum on the size of disjunction.
      if (++numMatches > 100)
        return EqExpr_create(cl, rd);

      ref<Expr> mayBe =
        EqExpr::create(rd->index, ConstantExpr::alloc(i,
                                                      rd->index->getWidth()));
      res = OrExpr::create(res, mayBe);
    }
  }

  return res;
}

static ref<Expr> EqExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r)
{
	Expr::Width	width = cl->getWidth();
	Expr::Kind	rk = r->getKind();

	if (width == Expr::Bool) {
		if (cl->isTrue())
			return r;

		// 0 == ...
		if (rk == Expr::Eq) {
			const EqExpr *ree = cast<EqExpr>(r);
			ConstantExpr *CE = dyn_cast<ConstantExpr>(ree->left);

			// eliminate double negation
			// 0 == (0 == A) => A
			if (CE && CE->getWidth() == Expr::Bool && CE->isFalse())
				return ree->right;
		} else if (rk == Expr::Or) {
			const OrExpr *roe = cast<OrExpr>(r);

			// transform not(or(a,b)) to and(not a, not b)
			return AndExpr::create(
				Expr::createIsZero(roe->left),
				Expr::createIsZero(roe->right));
		}
	} else if (rk == Expr::SExt) {
		// (sext(a,T)==c) == (a==c)
		const SExtExpr *see = cast<SExtExpr>(r);
		Expr::Width fromBits = see->src->getWidth();
		ref<ConstantExpr> trunc = cl->ZExt(fromBits);

		// pathological check, make sure it is possible to
		// sext to this value *from any value*
		if (cl == trunc->SExt(width))
			return EqExpr::create(see->src, trunc);

		return ConstantExpr::create(0, Expr::Bool);
	} else if (rk == Expr::ZExt) {
		// (zext(a,T)==c) == (a==c)
		const ZExtExpr *zee = cast<ZExtExpr>(r);
		Expr::Width fromBits = zee->src->getWidth();
		ref<ConstantExpr> trunc = cl->ZExt(fromBits);

		// pathological check, make sure it is possible to
		// zext to this value *from any value*
		if (cl == trunc->ZExt(width))
			return EqExpr::create(zee->src, trunc);

		return ConstantExpr::create(0, Expr::Bool);
	} else if (rk==Expr::Add) {
		const AddExpr *ae = cast<AddExpr>(r);
		if (isa<ConstantExpr>(ae->left)) {
			// c0 = c1 + b => c0 - c1 = b
			return EqExpr_createPartialR(
				cast<ConstantExpr>(
					SubExpr::create(cl, ae->left)),
				ae->right.get());
		}
	} else if (rk==Expr::Sub) {
		const SubExpr *se = cast<SubExpr>(r);
		if (isa<ConstantExpr>(se->left)) {
			// c0 = c1 - b => c1 - c0 = b
			return EqExpr_createPartialR(
				cast<ConstantExpr>(
					SubExpr::create(se->left, cl)),
					se->right.get());
		}
	} else if (rk == Expr::Read && ConstArrayOpt) {
		return TryConstArrayOpt(cl, static_cast<ReadExpr*>(r));
	} else if (rk == Expr::Concat) {
		ConcatExpr *ce = cast<ConcatExpr>(r);
		return AndExpr::create(
			EqExpr_createPartialR(
				cl->Extract(
					ce->getRight()->getWidth(),
					ce->getLeft()->getWidth()),
				ce->getLeft().get()),
			EqExpr_createPartialR(
				cl->Extract(0,
					ce->getRight()->getWidth()),
					ce->getRight().get()));

	}

	return EqExpr_create(cl, r);
}

static ref<Expr> EqExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr) {
  return EqExpr_createPartialR(cr, l);
}

ref<Expr> NeExpr::create(const ref<Expr> &l, const ref<Expr> &r) {
  return EqExpr::create(ConstantExpr::create(0, Expr::Bool),
                        EqExpr::create(l, r));
}

ref<Expr> UgtExpr::create(const ref<Expr> &l, const ref<Expr> &r) {
  return UltExpr::create(r, l);
}
ref<Expr> UgeExpr::create(const ref<Expr> &l, const ref<Expr> &r) {
  return UleExpr::create(r, l);
}

ref<Expr> SgtExpr::create(const ref<Expr> &l, const ref<Expr> &r) {
  return SltExpr::create(r, l);
}
ref<Expr> SgeExpr::create(const ref<Expr> &l, const ref<Expr> &r) {
  return SleExpr::create(r, l);
}

static ref<Expr> UltExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  Expr::Width t = l->getWidth();
  if (t == Expr::Bool) { // !l && r
    return AndExpr::create(Expr::createIsZero(l), r);
  } else if (l == r) {
    return ConstantExpr::alloc(0, Expr::Bool);
  } else {
    return UltExpr::alloc(l, r);
  }
}

static ref<Expr> UleExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l->getWidth() == Expr::Bool) { // !(l && !r)
    return OrExpr::create(Expr::createIsZero(l), r);
  } else if (r->isZero() || l == r) {
    return EqExpr::create(l, r);
  } else {
    return UleExpr::alloc(l, r);
  }
}

static ref<Expr> SltExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l->getWidth() == Expr::Bool) { // l && !r
    return AndExpr::create(l, Expr::createIsZero(r));
  } else {
    return SltExpr::alloc(l, r);
  }
}

static ref<Expr> SleExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l->getWidth() == Expr::Bool) { // !(!l && r)
    return OrExpr::create(l, Expr::createIsZero(r));
  } else {
    return SleExpr::alloc(l, r);
  }
}

CMPCREATE_T(EqExpr, Eq, EqExpr, EqExpr_createPartial, EqExpr_createPartialR)
CMPCREATE(UltExpr, Ult)
CMPCREATE(UleExpr, Ule)
CMPCREATE(SltExpr, Slt)
CMPCREATE(SleExpr, Sle)

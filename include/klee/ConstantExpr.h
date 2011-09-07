#ifndef KLEE_CONSTANTEXPR_H
#define KLEE_CONSTANTEXPR_H

#include "klee/util/Bits.h"
#include "klee/util/Ref.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/SmallVector.h"

#include <set>
#include <vector>
#include <map>
#include <iosfwd> // FIXME: Remove this!!!

namespace llvm {
  class Type;
  class Value;
}

namespace klee {

class Array;
class ConstantExpr;
class ObjectState;
class StateRecord;

template<class T> class ref;

// Terminal Exprs
class ConstantExpr : public Expr
{
public:
  static const Kind kind = Constant;
  static const unsigned numKids = 0;

private:
  llvm::APInt value;

protected:
  ConstantExpr(const llvm::APInt &v) : value(v) {}
  static void initSmallValTab(void);

public:
  ~ConstantExpr() {}
  
  Width getWidth() const { return value.getBitWidth(); }
  Kind getKind() const { return Constant; }

  unsigned getNumKids() const { return 0; }
  ref<Expr> getKid(unsigned i) const { return 0; }

  /// getAPValue - Return the arbitrary precision value directly.
  ///
  /// Clients should generally not use the APInt value directly and instead use
  /// native ConstantExpr APIs.
  const llvm::APInt &getAPValue() const { return value; }

  /// getZExtValue - Return the constant value for a limited number of bits.
  ///
  /// This routine should be used in situations where the width of the constant
  /// is known to be limited to a certain number of bits.
  uint64_t getZExtValue(unsigned bits = 64) const {
    assert(getWidth() <= bits && "Value may be out of range!");
    return value.getZExtValue();
  }

  /// getLimitedValue - If this value is smaller than the specified limit,
  /// return it, otherwise return the limit value.
  uint64_t getLimitedValue(uint64_t Limit = ~0ULL) const {
    return value.getLimitedValue(Limit);
  }

  /// toString - Return the constant value as a decimal string.
  void toString(std::string &Res) const;
 
  int compareContents(const Expr &b) const { 
    const ConstantExpr &cb = static_cast<const ConstantExpr&>(b);
    if (getWidth() != cb.getWidth()) 
      return getWidth() < cb.getWidth() ? -1 : 1;
    if (value == cb.value)
      return 0;
    return value.ult(cb.value) ? -1 : 1;
  }

  virtual ref<Expr> rebuild(ref<Expr> kids[]) const { 
    assert(0 && "rebuild() on ConstantExpr"); 
    return (Expr*) this;
  }

  virtual unsigned computeHash();
  
  static ref<Expr> fromMemory(void *address, Width w);
  void toMemory(void *address);

  static ref<ConstantExpr> alloc(const llvm::APInt &v);

  static ref<ConstantExpr> alloc(const llvm::APFloat &f) {
    return alloc(f.bitcastToAPInt());
  }

  static ref<ConstantExpr> alloc(uint64_t v, Width w) {
    return alloc(llvm::APInt(w, v));
  }
  
  static ref<ConstantExpr> create(uint64_t v, Width w) {
    assert(v == bits64::truncateToNBits(v, w) &&
           "invalid constant");
    return alloc(v, w);
  }

  static ref<ConstantExpr> createVector(llvm::ConstantVector* v);

  static bool classof(const Expr *E) {
    return E->getKind() == Expr::Constant;
  }
  static bool classof(const ConstantExpr *) { return true; }

  /* Utility Functions */

  /// isZero - Is this a constant zero.
  bool isZero() const { return value == llvm::APInt(value.getBitWidth(), 0); }

  /// isOne - Is this a constant one.
  bool isOne() const { return value == llvm::APInt(value.getBitWidth(), 1); }
  
  /// isTrue - Is this the true expression.
  bool isTrue() const { 
    return getZExtValue(1) == 1;
  }

  /// isFalse - Is this the false expression.
  bool isFalse() const {
    return getZExtValue(1) == 0;
  }

  /// isAllOnes - Is this constant all ones.
  bool isAllOnes() const {
    return value.isAllOnesValue();
  }

  /* Constant Operations */

  ref<ConstantExpr> Concat(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> Extract(unsigned offset, Width W);
  ref<ConstantExpr> ZExt(Width W);
  ref<ConstantExpr> SExt(Width W);
  ref<ConstantExpr> Add(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> Sub(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> Mul(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> UDiv(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> SDiv(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> URem(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> SRem(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> And(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> Or(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> Xor(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> Shl(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> LShr(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> AShr(const ref<ConstantExpr> &RHS);

  // Comparisons return a constant expression of width 1.

  ref<ConstantExpr> Eq(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> Ne(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> Ult(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> Ule(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> Ugt(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> Uge(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> Slt(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> Sle(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> Sgt(const ref<ConstantExpr> &RHS);
  ref<ConstantExpr> Sge(const ref<ConstantExpr> &RHS);

  ref<ConstantExpr> Neg();
  ref<ConstantExpr> Not();
};

static inline ref<ConstantExpr> createConstantExpr(uint64_t v, Expr::Width w)
{
	return ConstantExpr::create(v, w);
}

} // End klee namespace

#endif

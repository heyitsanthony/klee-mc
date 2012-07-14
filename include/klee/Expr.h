//===-- Expr.h --------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_EXPR_H
#define KLEE_EXPR_H

#include "klee/util/Bits.h"
#include "klee/util/Ref.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/SmallVector.h"

#include <stdio.h>
#include <set>
#include <vector>
#include <map>
#include <stack>

// from stp/c_interface.h, ugly dependency
extern "C" { const char* exprName(void* e); }

namespace llvm {
  class Type;
  class Value;
  class ConstantVector;
}

namespace klee {

class ExprBuilder;
class ExprAlloc;
class Array;
class ConstantExpr;
class ObjectState;

template<class T> class ref;

struct ArrayLT { bool operator()(const Array *a, const Array *b) const; };


#define MK_ZEXT(x,y)		ZExtExpr::create(x,y)
#define MK_SEXT(x,y)		SExtExpr::create(x,y)
#define MK_READ(x,y)		ReadExpr::create(x,y)
#define MK_CONST(x,y)		ConstantExpr::create(x,y)
#define MK_SELECT(x,y,z)	SelectExpr::create(x,y,z)
#define MK_CONCAT(x,y)		ConcatExpr::create(x,y)
#define MK_EXTRACT(x,y,z)	ExtractExpr::create(x,y,z)
#define MK_ADD(x,y)		AddExpr::create(x,y)
#define MK_SUB(x,y)		SubExpr::create(x,y)
#define MK_MUL(x,y)		MulExpr::create(x,y)
#define MK_UDIV(x,y)		UDivExpr::create(x,y)
#define MK_SDIV(x,y)		SDivExpr::create(x,y)
#define MK_UREM(x,y)		URemExpr::create(x,y)
#define MK_SREM(x,y)		SRemExpr::create(x,y)
#define MK_NOT(x,y)		NotExpr::create(x,y)
#define MK_OR(x,y)		OrExpr::create(x,y)
#define MK_AND(x,y)		AndExpr::create(x,y)
#define MK_XOR(x,y)		XorExpr::create(x,y)
#define MK_SHL(x,y)		ShlExpr::create(x,y)
#define MK_LSHR(x,y)		LShrExpr::create(x,y)
#define ML_ASHR(x,y)		AShrExpr::create(x,y)
#define MK_EQ(x,y)		EqExpr::create(x,y)
#define MK_NE(x,y)		NeExpr::create(x,y)
#define MK_ULT(x,y)		UltExpr::create(x,y)
#define MK_ULE(x,y)		UleExpr::create(x,y)
#define MK_UGT(x,y)		UgtExpr::create(x,y)
#define MK_SLT(x,y)		SltExpr::create(x,y)
#define MK_SLE(x,y)		SleExpr::create(x,y)
#define MK_SGT(x,y)		SgtExpr::create(x,y)
#define MK_SGE(x,y)		SgeExpr::create(x,y)

/// Class representing symbolic expressions.
/**

<b>Expression canonicalization</b>: we define certain rules for
canonicalization rules for Exprs in order to simplify code that
pattern matches Exprs (since the number of forms are reduced), to open
up further chances for optimization, and to increase similarity for
caching and other purposes.

The general rules are:
<ol>
<li> No Expr has all constant arguments.</li>

<li> Booleans:
    <ol type="a">
     <li> \c Ne, \c Ugt, \c Uge, \c Sgt, \c Sge are not used </li>
     <li> The only acceptable operations with boolean arguments are
          \c Not \c And, \c Or, \c Xor, \c Eq,
	  as well as \c SExt, \c ZExt,
          \c Select and \c NotOptimized. </li>
     <li> The only boolean operation which may involve a constant is boolean not (<tt>== false</tt>). </li>
     </ol>
</li>

<li> Linear Formulas:
   <ol type="a">
   <li> For any subtree representing a linear formula, a constant
   term must be on the LHS of the root node of the subtree.  In particular,
   in a BinaryExpr a constant must always be on the LHS.  For example, subtraction
   by a constant c is written as <tt>add(-c, ?)</tt>.  </li>
    </ol>
</li>


<li> Chains are unbalanced to the right </li>

</ol>


<b>Steps required for adding an expr</b>:
   -# Add case to printKind
   -# Add to ExprVisitor
   -# Add to IVC (implied value concretization) if possible


Todo: Shouldn't bool \c Xor just be written as not equal?

*/

class Expr
{
public:
  typedef uint64_t	Hash;
  static unsigned long	count;
  static unsigned int	errors;
  static ref<Expr>	errorExpr;
  static const unsigned MAGIC_HASH_CONSTANT = 39;


  /// The type of an expression is simply its width, in bits.
  typedef unsigned Width;

  static const Width InvalidWidth = 0;
  static const Width Bool = 1;
  static const Width Int8 = 8;
  static const Width Int16 = 16;
  static const Width Int32 = 32;
  static const Width Int64 = 64;
  static const Width Fl80 = 80;


  enum Kind {
    InvalidKind = -1,

    // Primitive

    Constant = 0,

    // Special

    /// Prevents optimization below the given expression.  Used for
    /// testing: make equality constraints that KLEE will not use to
    /// optimize to concretes.
    NotOptimized,

    //// Skip old varexpr, just for deserialization, purge at some point
    Read=NotOptimized+2, Select, Concat, Extract,

    // Casting,
    ZExt, SExt,

    // All subsequent kinds are binary.

    // Arithmetic
    Add, Sub, Mul, UDiv, SDiv, URem, SRem,

    // Bit
    Not, And, Or, Xor, Shl, LShr, AShr,

    // Compare
    Eq,
    Ne,  /// Not used in canonical form
    Ult,
    Ule,
    Ugt, /// Not used in canonical form
    Uge, /// Not used in canonical form
    Slt,
    Sle,
    Sgt, /// Not used in canonical form
    Sge, /// Not used in canonical form

    Let, Bind,

    LastKind=Bind,

    CastKindFirst=ZExt,
    CastKindLast=SExt,
    BinaryKindFirst=Add,
    BinaryKindLast=Sge,
    CmpKindFirst=Eq,
    CmpKindLast=Sge
  };

protected:
  static ExprBuilder	*theExprBuilder;
  static ExprAlloc	*theExprAllocator;

  // insensitive to array names
  Hash hashValue;

  // insensitive to constant values and arrays (e.g. names AND size)
  Hash skeletonHash;

  Expr() : refCount(0) { count++; }

public:
  unsigned refCount;

  virtual ~Expr() { count--; }
  static unsigned long getNumExprs(void) { return count; }
  static ExprBuilder* setBuilder(ExprBuilder* builder);
  static ExprAlloc* setAllocator(ExprAlloc* alloc);
  static ExprBuilder* getBuilder(void) {return theExprBuilder;}
  static ExprAlloc* getAllocator(void) { return theExprAllocator; }
  static void resetErrors(void) { errors = 0; errorExpr = NULL; }
  static ref<Expr> createBoothMul(const ref<Expr>& expr, uint64_t v);
  static ref<Expr> createShiftAddMul(const ref<Expr>& expr, uint64_t v);

  virtual Kind getKind() const = 0;
  virtual Width getWidth() const = 0;

  virtual unsigned getNumKids() const = 0;
  virtual ref<Expr> getKid(unsigned i) const = 0;
  virtual const Expr* getKidConst(unsigned i) const = 0;

  virtual void print(std::ostream &os) const;

  /// dump - Print the expression to stderr.
  void dump() const;

  /// Returns the pre-computed hash of the current expression
  Hash hash() const { return hashValue; }
  Hash skeleton() const { return skeletonHash; }

  /// (Re)computes the hash of the current expression.
  /// Returns the hash value.
  virtual Hash computeHash();

  // Returns
  // 0 iff b is structuraly equivalent to *this
  // -1 if this < b
  // 1 if this > b
  // David says this gets called a billion times a second, and
  // most of the comparisons are equal ptrs.
  // So, it's probably wise to inline that case.
  inline int compare(const Expr &b) const
  {
	if (this == &b) return 0;
	if (hashValue < b.hashValue) return -1;
	if (hashValue > b.hashValue) return 1;
	return compareSlow(b);
  }
  virtual int compareContents(const Expr &b) const { return 0; }

  // Given an array of new kids return a copy of the expression
  // but using those children.
  virtual ref<Expr> rebuild(ref<Expr> kids[/* getNumKids() */]) const = 0;

  // reconstruct expression from bottom up
  ref<Expr> rebuild(void) const;

  /// isZero - Is this a constant zero.
  bool isZero() const;
  /// Is this the true/false expression.
  bool isTrue() const;
  bool isFalse() const;

  /* Static utility methods */

  static void printKind(std::ostream &os, Kind k);
  static void printWidth(std::ostream &os, Expr::Width w);

  /// returns the smallest number of bytes in which the given width fits
  static inline unsigned getMinBytesForWidth(Width w) { return (w + 7) / 8; }

  /* Kind utilities */

  /* Utility creation functions */
  static ref<Expr> createCoerceToPointerType(ref<Expr> e);
  static ref<Expr> createImplies(ref<Expr> hyp, ref<Expr> conc);
  static ref<Expr> createIsZero(ref<Expr> e);

  /// Create a little endian read of the given type at offset 0 of the
  /// given object.
  static ref<Expr> createTempRead(
	const ref<Array>& array, Expr::Width w, unsigned arr_off = 0);

  static ref<ConstantExpr> createPointer(uint64_t v);

  struct CreateArg;
  static ref<Expr> createFromKind(Kind k, std::vector<CreateArg> args);

  static bool isValidKidWidth(unsigned kid, Width w) { return true; }

  static bool classof(const Expr *) { return true; }

  int compareSlow(const Expr& b) const;
  int compareDeep(const Expr& b) const;

  static Hash hashImpl(const void* data, size_t len, Hash hash);
};

struct Expr::CreateArg {
  ref<Expr> expr;
  Width width;

  CreateArg(Width w = Bool) : expr(0), width(w) {}
  CreateArg(ref<Expr> e) : expr(e), width(Expr::InvalidWidth) {}

  bool isExpr() { return !isWidth(); }
  bool isWidth() { return width != Expr::InvalidWidth; }
};

// Comparison operators

inline bool operator==(const Expr &lhs, const Expr &rhs)
{
	if (&lhs == &rhs)
		return true;
	if (lhs.hash() != rhs.hash())
		return false;
	return lhs.compare(rhs) == 0;
}

inline bool operator!=(const Expr &lhs, const Expr &rhs)
{
	if (&lhs == &rhs)
		return false;
	if (lhs.hash() != rhs.hash())
		return true;
	return !(lhs == rhs);
}

inline bool operator<(const Expr &lhs, const Expr &rhs)
{
	if (&lhs == &rhs) return false;
	return lhs.compare(rhs) < 0;
}

inline bool operator>(const Expr &lhs, const Expr &rhs) { return rhs < lhs; }
inline bool operator<=(const Expr &lhs, const Expr &rhs) { return !(lhs > rhs);}
inline bool operator>=(const Expr &lhs, const Expr &rhs) { return !(lhs < rhs);}

// Printing operators

inline std::ostream &operator<<(std::ostream &os, const Expr &e) {
  e.print(os);
  return os;
}

inline std::ostream &operator<<(std::ostream &os, const Expr::Kind kind) {
  Expr::printKind(os, kind);
  return os;
}


// Utility classes

class NonConstantExpr : public Expr
{
public:
	static bool classof(const Expr *E)
	{ return E->getKind() != Expr::Constant; }
	static bool classof(const NonConstantExpr *) { return true; }
};

class BinaryExpr : public NonConstantExpr
{
public:
	ref<Expr> left, right;

public:
	unsigned getNumKids() const { return 2; }
	ref<Expr> getKid(unsigned i) const
	{
		if(i == 0) return left;
		if(i == 1) return right;
		return 0;
	}

	const Expr* getKidConst(unsigned i) const
	{
		if (i == 0) return left.get();
		if (i == 1) return right.get();
		return NULL;
	}

	static ref<Expr> create(Kind k, const ref<Expr> &l, const ref<Expr> &r);
protected:
	BinaryExpr(const ref<Expr> &l, const ref<Expr> &r) : left(l), right(r) {}

public:
	static bool classof(const Expr *E) {
		Kind k = E->getKind();
		return Expr::BinaryKindFirst <= k && k <= Expr::BinaryKindLast;
	}
	static bool classof(const BinaryExpr *) { return true; }
};


class CmpExpr : public BinaryExpr {

protected:
  CmpExpr(ref<Expr> l, ref<Expr> r) : BinaryExpr(l,r) {}

public:
  Width getWidth() const { return Bool; }

  static bool classof(const Expr *E) {
    Kind k = E->getKind();
    return Expr::CmpKindFirst <= k && k <= Expr::CmpKindLast;
  }
  static bool classof(const CmpExpr *) { return true; }
};

// Special

class NotOptimizedExpr : public NonConstantExpr
{
public:
  static const Kind kind = NotOptimized;
  static const unsigned numKids = 1;
  NotOptimizedExpr(const ref<Expr> &_src, unsigned _tag=0)
  : src(_src), tag(_tag) {}

  ref<Expr> src;

  static ref<Expr> alloc(const ref<Expr> &src);
  static ref<Expr> create(ref<Expr> src);

  Width getWidth() const { return src->getWidth(); }
  Kind getKind() const { return NotOptimized; }

  unsigned getNumKids() const { return 1; }
  ref<Expr> getKid(unsigned i) const { return src; }
  const Expr* getKidConst(unsigned i) const { return src.get(); }
  virtual ref<Expr> rebuild(ref<Expr> kids[]) const
  {
  	ref<Expr>	e(create(kids[0]));
	cast<NotOptimizedExpr>(e)->tag = tag;
	return e;
  }

  static bool classof(const Expr *E)
  { return E->getKind() == Expr::NotOptimized; }

  static bool classof(const NotOptimizedExpr *) { return true; }
  virtual Hash computeHash(void);

  void setTag(unsigned t) { tag = t; }
  unsigned getTag(void) const { return tag; }
private:
  unsigned tag;
};


/// Class representing a byte update of an array.
class UpdateNode
{
	friend class UpdateList;
	friend class STPBuilder; // for setting STPArray

	mutable unsigned refCount;
	// gross
	mutable void *stpArray;
	// cache instead of recalc
	Expr::Hash hashValue;

public:
	const UpdateNode *next;
	ref<Expr> index, value;

	mutable void *btorArray;
	mutable void *z3Array;
private:
	/// size of this update sequence, including this update
	unsigned size;

public:
	UpdateNode(const UpdateNode *_next,
	     const ref<Expr> &_index,
	     const ref<Expr> &_value);

	unsigned getSize() const { return size; }

	int compare(const UpdateNode &b) const;
	Expr::Hash hash() const { return hashValue; }

private:
	UpdateNode()
	: refCount(0), stpArray(0) , btorArray(0), z3Array(0) {}
	~UpdateNode();

	Expr::Hash computeHash();
};

#include "klee/MallocKey.h"

class Array;

/// Class representing a complete list of updates into an array.
class UpdateList
{
	friend class ReadExpr; // for default constructor
public:
	UpdateList(const ref<Array>& _root, const UpdateNode *_head);
	UpdateList(const UpdateList &b);
	~UpdateList();

	UpdateList &operator=(const UpdateList &b);

	/// size of this update list
	unsigned getSize() const { return (head ? head->getSize() : 0); }

	void extend(const ref<Expr> &index, const ref<Expr> &value);

	int compare(const UpdateList &b) const;
	Expr::Hash hash() const
	{
		if (hashValue) return hashValue;
		hashValue = computeHash();
		return hashValue;
	}

	static UpdateList* fromUpdateStack(
		const Array* arr,
		std::stack<std::pair<ref<Expr>, ref<Expr> > >& updateStack);

	const ref<Array> getRoot(void) const { return root; }
	static unsigned getCount(void) { return totalUpdateLists; }
private:
	Expr::Hash	computeHash(void) const;
	void removeDups(const ref<Expr>& index);
	static unsigned		totalUpdateLists;
	const ref<Array>	root;
	mutable Expr::Hash	hashValue;
public:
	/// pointer to the most recent update node
	const UpdateNode *head;
};


#include "klee/Array.h"

/// Class representing a one byte read from an array.
class ReadExpr : public NonConstantExpr {
public:
  static const Kind kind = Read;
  static const unsigned numKids = 1;

  ReadExpr(const UpdateList &_updates, const ref<Expr> &_index)
  : updates(_updates), index(_index) {}

  UpdateList updates;
  ref<Expr> index;

  const ref<Array> getArray(void) const { return updates.root; }
  bool hasUpdates(void) const { return updates.head != NULL; }

  static ref<Expr> alloc(const UpdateList &updates, const ref<Expr> &index);
  static ref<Expr> create(const UpdateList &updates, ref<Expr> i);

  Width getWidth() const { return Expr::Int8; }
  Kind getKind() const { return Read; }

  unsigned getNumKids() const { return numKids; }
  ref<Expr> getKid(unsigned i) const { return !i ? index : 0; }
  const Expr* getKidConst(unsigned i) const { return !i ? index.get() : NULL; }

  int compareContents(const Expr &b) const;

  virtual ref<Expr> rebuild(ref<Expr> kids[]) const
  { return create(updates, kids[0]); }

  virtual Expr::Hash computeHash();

  static bool classof(const Expr *E) { return E->getKind() == Expr::Read; }
  static bool classof(const ReadExpr *) { return true; }
};


class LetExpr : public NonConstantExpr
{
public:
	static const Kind kind = Let;
	static const unsigned numKids = 2;

	virtual ~LetExpr() {}
	unsigned getNumKids() const { return numKids; }
	
	static ref<Expr> alloc(const ref<Expr>& b_expr, const ref<Expr>& s_expr)
	{
		ref<Expr> r(new LetExpr(b_expr, s_expr));
		r->computeHash();
		return r;
	}

	ref<Expr> getKid(unsigned i) const
	{
		assert (i < 2);
		return (i == 0) ? binding_expr : scope_expr;
	}


	const Expr* getKidConst(unsigned i) const
	{ assert (i < 2);
	  return (i == 0) ? binding_expr.get() : scope_expr.get(); }

	Kind getKind() const { return Let; }
	static bool classof(const Expr *E) { return E->getKind() == Expr::Let; }
	static bool classof(const LetExpr *l) { return true; }
	Width getWidth(void) const { return scope_expr->getWidth(); }

	ref<Expr> getBindExpr(void) const { return binding_expr; }
	ref<Expr> getScopeExpr(void) const { return scope_expr; }

	ref<Expr> rescope(ref<Expr>& s_expr) const
	{
		ref<Expr> r(new LetExpr(binding_expr, s_expr, id));
		r->computeHash();
		return r;
	}

	ref<Expr> rebuild(ref<Expr> kids[/* getNumKids() */]) const
	{
		return alloc(kids[0], kids[1]);
	}

	uint64_t getId(void) const { return id; }

protected:
	static uint64_t	next_id;

	LetExpr(const ref<Expr> &b_expr, const ref<Expr> &s_expr)
	: id(++next_id)
	, binding_expr(b_expr)
	, scope_expr(s_expr)
	{}

	LetExpr(const ref<Expr> &b_expr, const ref<Expr> &s_expr,
		uint64_t in_id)
	: id(in_id)
	, binding_expr(b_expr)
	, scope_expr(s_expr)
	{}


private:
	friend class BindExpr;
	uint64_t	id;
	ref<Expr>	binding_expr;
	ref<Expr>	scope_expr;
};

class BindExpr : public NonConstantExpr
{
public:
	static const Kind kind = Bind;
	static const unsigned numKids = 0;
	virtual ~BindExpr() {}
	unsigned getNumKids() const { return 0; }
	ref<Expr> getKid(unsigned i) const { return NULL; }
	const Expr* getKidConst(unsigned i) const { return NULL; }
	Kind getKind() const { return kind; }
	Expr::Hash computeHash(void);

	static ref<Expr> alloc(const ref<LetExpr> &l) {
		ref<Expr> c(new BindExpr(l));
		c->computeHash();
		return c;
	}

	ref<Expr> rebuild(ref<Expr> kids[]) const { return alloc(let_expr); }


	static bool classof(const Expr *E) { return E->getKind() == Expr::Bind; }
	static bool classof(const BindExpr *) { return true; }

	Width getWidth(void) const
	{ return let_expr->binding_expr->getWidth(); }

	ref<LetExpr>	let_expr;

protected:
	BindExpr(const ref<LetExpr> &l) : let_expr(l) {}
private:
};

/// Class representing an if-then-else expression.
class SelectExpr : public NonConstantExpr {
public:
  static const Kind kind = Select;
  static const unsigned numKids = 3;

  SelectExpr(const ref<Expr> &c, const ref<Expr> &t, const ref<Expr> &f)
    : cond(c), trueExpr(t), falseExpr(f) {}

  ref<Expr> cond, trueExpr, falseExpr;

  static ref<Expr> alloc(
  	const ref<Expr> &c, const ref<Expr> &t, const ref<Expr> &f);
  static ref<Expr> create(ref<Expr> c, ref<Expr> t, ref<Expr> f);

  Width getWidth() const { return trueExpr->getWidth(); }
  Kind getKind() const { return Select; }

  unsigned getNumKids() const { return numKids; }
  ref<Expr> getKid(unsigned i) const {
        switch(i) {
        case 0: return cond;
        case 1: return trueExpr;
        case 2: return falseExpr;
        default: return 0;
        }
   }


    const Expr* getKidConst(unsigned i) const
    { if (i == 0) return cond.get();
      if (i == 1) return trueExpr.get();
      if (i == 2) return falseExpr.get();
      return NULL; }

  static bool isValidKidWidth(unsigned kid, Width w) {
    if (kid == 0)
      return w == Bool;
    else
      return true;
  }

  virtual ref<Expr> rebuild(ref<Expr> kids[]) const
  { return create(kids[0], kids[1], kids[2]); }

  static bool classof(const Expr *E) {
    return E->getKind() == Expr::Select;
  }
  static bool classof(const SelectExpr *) { return true; }
};


/** Children of a concat expression can have arbitrary widths.
    Kid 0 is the left kid, kid 1 is the right kid.
*/
class ConcatExpr : public NonConstantExpr {
public:
  static const Kind kind = Concat;
  static const unsigned numKids = 2;
  
  ConcatExpr(const ref<Expr> &l, const ref<Expr> &r)
  : left(l), right(r)
  { width = l->getWidth() + r->getWidth(); }

  static ref<Expr> alloc(const ref<Expr> &l, const ref<Expr> &r);
  static ref<Expr> create(const ref<Expr> &l, const ref<Expr> &r);

  Width getWidth() const { return width; }
  Kind getKind() const { return kind; }
  ref<Expr> getLeft() const { return left; }
  ref<Expr> getRight() const { return right; }
  static ref<Expr> mergeExtracts(const ref<Expr>& l, const ref<Expr>& r);


  unsigned getNumKids() const { return numKids; }
  ref<Expr> getKid(unsigned i) const {
    if (i == 0) return left;
    else if (i == 1) return right;
    else return NULL;
  }

   const Expr* getKidConst(unsigned i) const
   { if (i == 0) return left.get();
     if (i == 1) return right.get();
     return NULL; }

  /// Shortcuts to create larger concats.  The chain returned is unbalanced to the right
  static ref<Expr> createN(unsigned nKids, const ref<Expr> kids[]);
  static ref<Expr> create4(const ref<Expr> &kid1, const ref<Expr> &kid2,
			   const ref<Expr> &kid3, const ref<Expr> &kid4);
  static ref<Expr> create8(const ref<Expr> &kid1, const ref<Expr> &kid2,
			   const ref<Expr> &kid3, const ref<Expr> &kid4,
			   const ref<Expr> &kid5, const ref<Expr> &kid6,
			   const ref<Expr> &kid7, const ref<Expr> &kid8);

  virtual ref<Expr> rebuild(ref<Expr> kids[]) const
  {return create(kids[0], kids[1]); }

  static bool classof(const Expr *E)
  { return E->getKind() == Expr::Concat; }

  static bool classof(const ConcatExpr *) { return true; }

private:
  Width width;
  ref<Expr> left, right;
};


/** This class represents an extract from expression {\tt expr}, at
    bit offset {\tt offset} of width {\tt width}.  Bit 0 is the right most
    bit of the expression.
 */
class ExtractExpr : public NonConstantExpr {
public:
  static const Kind kind = Extract;
  static const unsigned numKids = 1;

  ExtractExpr(const ref<Expr> &e, unsigned b, Width w)
  : expr(e),offset(b),width(w) {}

  ref<Expr> expr;
  unsigned offset;
  Width width;

  static ref<Expr> alloc(const ref<Expr> &e, unsigned o, Width w); 
  /// Creates an ExtractExpr with the given bit offset and width
  static ref<Expr> create(ref<Expr> e, unsigned bitOff, Width w);

  Width getWidth() const { return width; }
  Kind getKind() const { return Extract; }

  unsigned getNumKids() const { return numKids; }
  ref<Expr> getKid(unsigned i) const { return expr; }
  const Expr* getKidConst(unsigned i) const { return expr.get(); }

  int compareContents(const Expr &b) const {
    const ExtractExpr &eb = static_cast<const ExtractExpr&>(b);
    if (offset != eb.offset) return offset < eb.offset ? -1 : 1;
    if (width != eb.width) return width < eb.width ? -1 : 1;
    return 0;
  }

  virtual ref<Expr> rebuild(ref<Expr> kids[]) const
  { return create(kids[0], offset, width); }

  virtual Hash computeHash();

  static bool classof(const Expr *E) { return E->getKind() == Expr::Extract; }
  static bool classof(const ExtractExpr *) { return true; }
};


/**
    Bitwise Not
*/
class NotExpr : public NonConstantExpr
{
public:
  static const Kind kind = Not;
  static const unsigned numKids = 1;
  NotExpr(const ref<Expr> &e) : expr(e) {}

  ref<Expr> expr;

  static ref<Expr> alloc(const ref<Expr> &e);
  static ref<Expr> create(const ref<Expr> &e);

  Width getWidth() const { return expr->getWidth(); }
  Kind getKind() const { return Not; }

  unsigned getNumKids() const { return numKids; }
  ref<Expr> getKid(unsigned i) const { return expr; }
  const Expr* getKidConst(unsigned i) const { return expr.get(); }

  int compareContents(const Expr &b) const {
    const NotExpr &eb = static_cast<const NotExpr&>(b);
    if (expr != eb.expr) return expr < eb.expr ? -1 : 1;
    return 0;
  }

  virtual ref<Expr> rebuild(ref<Expr> kids[]) const { return create(kids[0]); }

  virtual Hash computeHash();

  static bool classof(const Expr *E) { return E->getKind() == Expr::Not; }
  static bool classof(const NotExpr *) { return true; }
};



// Casting

class CastExpr : public NonConstantExpr
{
public:
  ref<Expr> src;
  Width width;

  CastExpr(const ref<Expr> &e, Width w) : src(e), width(w) {}

  Width getWidth() const { return width; }

  unsigned getNumKids() const { return 1; }
  ref<Expr> getKid(unsigned i) const { return (i==0) ? src : 0; }
  const Expr* getKidConst(unsigned i) const { return (i==0) ? src.get() : 0; }

  int compareContents(const Expr &b) const {
    const CastExpr &eb = static_cast<const CastExpr&>(b);
    if (width != eb.width) return width < eb.width ? -1 : 1;
    return 0;
  }

  virtual Hash computeHash();

  static bool classof(const Expr *E) {
    Expr::Kind k = E->getKind();
    return Expr::CastKindFirst <= k && k <= Expr::CastKindLast;
  }
  static bool classof(const CastExpr *) { return true; }
};

#define CAST_EXPR_CLASS(_class_kind)                             \
class _class_kind ## Expr : public CastExpr {                    \
public:   \
  static const Kind kind = _class_kind;                          \
  static const unsigned numKids = 1;                             \
    _class_kind ## Expr(ref<Expr> e, Width w) : CastExpr(e,w) {} \
    static ref<Expr> alloc(const ref<Expr> &e, Width w);	\
    static ref<Expr> create(const ref<Expr> &e, Width w);        \
    Kind getKind() const { return _class_kind; }                 \
    virtual ref<Expr> rebuild(ref<Expr> kids[]) const {          \
      return create(kids[0], width);                             \
    }                                                            \
\
    static bool classof(const Expr *E)				 \
    { return E->getKind() == Expr::_class_kind; }                \
    static bool classof(const  _class_kind ## Expr *)		 \
    { return true;}                                              \
};                                                               \

CAST_EXPR_CLASS(SExt)
CAST_EXPR_CLASS(ZExt)

// Arithmetic/Bit Exprs

#define ARITHMETIC_EXPR_CLASS(_class_kind)                           \
class _class_kind ## Expr : public BinaryExpr {                      \
public:  \
  static const Kind kind = _class_kind;                              \
  static const unsigned numKids = 2;                                 \
    _class_kind ## Expr(const ref<Expr> &l,                          \
                        const ref<Expr> &r) : BinaryExpr(l,r) {}     \
    static ref<Expr> alloc(const ref<Expr> &l, const ref<Expr> &r);  \
    static ref<Expr> create(const ref<Expr> &l, const ref<Expr> &r); \
    Width getWidth() const { return left->getWidth(); }              \
    Kind getKind() const { return _class_kind; }                     \
    virtual ref<Expr> rebuild(ref<Expr> kids[]) const {              \
      return create(kids[0], kids[1]);                               \
    }                                                                \
\
    static bool classof(const Expr *E)	\
    { return E->getKind() == Expr::_class_kind; }                    \
    static bool classof(const  _class_kind ## Expr *) {return true;} \
};                                                                   \

ARITHMETIC_EXPR_CLASS(Add)
ARITHMETIC_EXPR_CLASS(Sub)
ARITHMETIC_EXPR_CLASS(Mul)
ARITHMETIC_EXPR_CLASS(UDiv)
ARITHMETIC_EXPR_CLASS(SDiv)
ARITHMETIC_EXPR_CLASS(URem)
ARITHMETIC_EXPR_CLASS(SRem)
ARITHMETIC_EXPR_CLASS(And)
ARITHMETIC_EXPR_CLASS(Or)
ARITHMETIC_EXPR_CLASS(Xor)
ARITHMETIC_EXPR_CLASS(Shl)
ARITHMETIC_EXPR_CLASS(LShr)
ARITHMETIC_EXPR_CLASS(AShr)

// Comparison Exprs

#define COMPARISON_EXPR_CLASS(_class_kind)                           \
class _class_kind ## Expr : public CmpExpr {                         \
public:  \
  static const Kind kind = _class_kind;                              \
  static const unsigned numKids = 2;                                 \
    _class_kind ## Expr(const ref<Expr> &l,                          \
                        const ref<Expr> &r) : CmpExpr(l,r) {}        \
    static ref<Expr> alloc(const ref<Expr> &l, const ref<Expr> &r);  \
    static ref<Expr> create(const ref<Expr> &l, const ref<Expr> &r); \
    Kind getKind() const { return _class_kind; }                     \
    virtual ref<Expr> rebuild(ref<Expr> kids[]) const			\
    { return create(kids[0], kids[1]); } 				\
    static bool classof(const Expr *E)				     \
    { return E->getKind() == Expr::_class_kind; }                    \
    static bool classof(const  _class_kind ## Expr *) { return true; } \
};                                                                   \

COMPARISON_EXPR_CLASS(Eq)
COMPARISON_EXPR_CLASS(Ne)
COMPARISON_EXPR_CLASS(Ult)
COMPARISON_EXPR_CLASS(Ule)
COMPARISON_EXPR_CLASS(Ugt)
COMPARISON_EXPR_CLASS(Uge)
COMPARISON_EXPR_CLASS(Slt)
COMPARISON_EXPR_CLASS(Sle)
COMPARISON_EXPR_CLASS(Sgt)
COMPARISON_EXPR_CLASS(Sge)
}

#include "klee/ConstantExpr.h"

namespace klee {
// Implementations

inline bool Expr::isZero() const {
  if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(this))
    return CE->isZero();
  return false;
}

inline bool Expr::isTrue() const {
  assert(getWidth() == Expr::Bool && "Invalid isTrue() call!");
  if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(this))
    return CE->isTrue();
  return false;
}

inline bool Expr::isFalse() const {
  assert(getWidth() == Expr::Bool && "Invalid isFalse() call!");
  if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(this))
    return CE->isFalse();
  return false;
}

} // End klee namespace

#endif

//===-- Expr.cpp ----------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "klee/Expr.h"
#include "klee/ExprBuilder.h"
#include "ExprRebuilder.h"
#include "klee/util/ConstantDivision.h"
#include "ExtraOptBuilder.h"

// FIXME: We shouldn't need this once fast constant support moves into
// Core. If we need to do arithmetic, we probably want to use APInt.
#include "llvm/Support/CommandLine.h"

#include "klee/util/ExprPPrinter.h"
#include "ExprAllocUnique.h"

#include <iostream>

using namespace klee;
using namespace llvm;

// silly expr factory singleton used to initialize builder on
// program startup
//
//
/* ugh! global ctors are awful! */
#if 0
namespace {
	cl::opt<bool>
	UseExprConsPtr(
		"expr-cons-ptr",
		cl::init(false),
		cl::desc("Unique expression => unique pointer"));

}
#endif

class ExprFactory
{
public:
	ExprFactory(void);
	virtual ~ExprFactory(void)
	{
		delete Expr::setBuilder(NULL);
		delete Expr::setAllocator(NULL);
	}
private:
};

ExprFactory	theExprFactory;
static bool	UseExprConsPtr = false;
ExprBuilder*	Expr::theExprBuilder = NULL;
ExprAlloc*	Expr::theExprAllocator = NULL;
ref<Expr>	Expr::errorExpr = NULL;
unsigned long	Expr::count = 0;
unsigned int	Expr::errors = 0;
uint64_t	LetExpr::next_id = 0;

bool ArrayLT::operator()(const Array *a, const Array *b) const
{ return *a < *b; }

ExprFactory::ExprFactory(void)
{
	Expr::setBuilder(ExprBuilder::create(ExprBuilder::ExtraOptsBuilder));
	if (UseExprConsPtr) {
		Expr::setAllocator(new ExprAllocUnique());
	} else {
		Expr::setAllocator(new ExprAlloc());
	}
//	Expr::setBuilder(
//		createSimplifyingExprBuilder(
//			createDefaultExprBuilder()));
}

ExprBuilder* Expr::setBuilder(ExprBuilder* builder)
{
	ExprBuilder	*oldBuilder = theExprBuilder;
	theExprBuilder = builder;
	return oldBuilder;
}

ExprAlloc* Expr::setAllocator(ExprAlloc* a)
{
	ExprAlloc	*oldAlloc = theExprAllocator;
	theExprAllocator = a;
	return oldAlloc;
}

ref<Expr> Expr::rebuild(void) const
{
	ExprRebuilder	r;
	return r.rebuild(this);
}

static ref<Expr> getTempReadBytes(
	const ref<Array> &array, unsigned bytes, unsigned arr_off)
{
	UpdateList	ul(array, NULL);
	ref<Expr>	kids[16];
	assert (bytes && bytes <= 16);

	for (unsigned i = 0; i < bytes; i++)
		kids[i] = ReadExpr::create(
			ul,
			ConstantExpr::create(arr_off+i,Expr::Int32));

	return ConcatExpr::createN(bytes, kids);
}

ref<Expr> Expr::createTempRead(
	const ref<Array> &array, Expr::Width w, unsigned arr_off)
{
	unsigned	w_bytes;
	ref<Expr>	ret;

	w_bytes = ((w + 7)/8);
	ret = getTempReadBytes(array, w_bytes, arr_off);
	if (w_bytes*8 == w)
		return ret;
	ret = ZExtExpr::create(ret, w);
	return ret;
}

int Expr::compareSlow(const Expr& b) const
{ return theExprAllocator->compare(*this, b); }

/* Slow path for comparison. This should only be used by Expr::compare */
int Expr::compareDeep(const Expr& b) const
{
	Kind ak = getKind(), bk = b.getKind();
	if (ak!=bk)
		return (ak < bk) ? -1 : 1;

	if (hashValue != b.hashValue/* && !isa<ConstantExpr>(*this)*/)
		return (hashValue < b.hashValue) ? -1 : 1;

	if (int res = compareContents(b))
		return res;

	unsigned aN = getNumKids();
	for (unsigned i=0; i<aN-1; i++)
		if (int res = getKidConst(i)->compare(*b.getKidConst(i)))
			return res;

	/* tail recursion? */
	return getKidConst(aN-1)->compare(*b.getKidConst(aN-1));
}

void Expr::printKind(std::ostream &os, Kind k)
{
	switch(k) {
#define X(C) case C: os << #C; break
	X(Constant);
	X(NotOptimized);
	X(Read);
	X(Select);
	X(Concat);
	X(Extract);
	X(ZExt); X(SExt);
	X(Add); X(Sub);	X(Mul); X(UDiv); X(SDiv); X(URem); X(SRem);
	X(Not); X(And); X(Or); X(Xor); X(Shl); X(LShr); X(AShr);
	X(Eq); X(Ne);
	X(Ult); X(Ule); X(Ugt); X(Uge); X(Slt); X(Sle); X(Sgt); X(Sge);
	X(Bind); X(Let);
#undef X
	default:
	assert(0 && "invalid kind");
	}
}

ref<Expr> Expr::createFromKind(Kind k, std::vector<CreateArg> args)
{
  unsigned numArgs = args.size();

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

      case Add: case Sub: case Mul: case UDiv: case SDiv: case URem: case SRem:
      case And: case Or: case Xor: case Shl: case LShr: case AShr:
      case Eq: case Ne:
      case Ult: case Ule: case Ugt: case Uge:
      case Slt: case Sle: case Sgt: case Sge:
        assert (numArgs == 2 &&
		args[0].isExpr() && args[1].isExpr() && "invalid args array");
        return BinaryExpr::create(k, args[0].expr, args[1].expr);
  }
}


void Expr::printWidth(std::ostream &os, Width width)
{
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

ref<Expr> Expr::createImplies(ref<Expr> hyp, ref<Expr> conc)
{ return OrExpr::create(Expr::createIsZero(hyp), conc); }

ref<Expr> Expr::createIsZero(ref<Expr> e)
{ return EqExpr::create(e, ConstantExpr::create(0, e->getWidth())); }

void Expr::print(std::ostream &os) const
{
	ref<Expr>	e(const_cast<Expr*>(this));
	ExprPPrinter::printSingleExpr(os, e);
}

void Expr::dump() const
{
	this->print(std::cerr);
	std::cerr << std::endl;
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
int MallocKey::compare(const MallocKey &a) const
{
	if (allocSite != a.allocSite || iteration != a.iteration)
		return (*this < a) ? -1 : 1;

	if (size < a.size)
		return -1;

	if (size == a.size)
		return 0;

	// size > a.size; check whether they share a lower bound
	std::set<uint64_t>::iterator it = seenSizes[*this].lower_bound(a.size);

	assert(it != seenSizes[*this].end());
	if (size <= *it)
		return 0;

	// this->size > lower bound, so *this > a
	return 1;
}

int ReadExpr::compareContents(const Expr &b) const
{ return updates.compare(static_cast<const ReadExpr&>(b).updates); }

ref<Expr> ConcatExpr::mergeExtracts(const ref<Expr>& l, const ref<Expr>& r)
{
	Expr::Width	w = l->getWidth() + r->getWidth();
	ExtractExpr	*ee_left, *ee_right;
	ConcatExpr	*con_right;

	ee_left = dyn_cast<ExtractExpr>(l);
	if (ee_left == NULL)
		return NULL;

	ee_right = dyn_cast<ExtractExpr>(r);
	if (ee_right != NULL) {
		if (	(ee_right->offset+ee_right->width)==ee_left->offset &&
			(ee_left->expr == ee_right->expr))
		{
			return ExtractExpr::create(
				ee_left->expr, ee_right->offset, w);
		}
	}

	// TODO: more than just add?
	if (r->getKind() == Expr::Add) {
		// (concat
		// 	(Extract w56 8 (Add 8 (Read64 x reg)))
		// 	(Add 8 (Read8 x read)))
		// =>
		// (Add (Extract w8 0 8) (Extract w8 0 (Read64 x reg)))
		ref<Expr>	ee_left_kid(ee_left->getKid(0));
		if (ee_left_kid->getKind() == r->getKind())
		{
			ref<Expr>	test_expr;
			test_expr = AddExpr::create(
				ExtractExpr::create(
					ee_left_kid->getKid(0),
					0,
					r->getWidth()),
				ExtractExpr::create(
					ee_left_kid->getKid(1),
					0,
					r->getWidth()));
			if (test_expr == r && ee_left_kid->getWidth() >= w) {
				/* lift useless extract */
				return ExtractExpr::create(ee_left_kid, 0, w);
			}
		}
	}


	// concat(extract(x[j+1]), concat(extract(x[j]), ...)
	//   => concat(extract(x[j+1:j]), ...)
	con_right = dyn_cast<ConcatExpr>(r);
	if (con_right != NULL) {
		ee_right = dyn_cast<ExtractExpr>(con_right->left);
		if (	ee_right &&
			ee_left->expr == ee_right->expr &&
			ee_left->offset == ee_right->offset + ee_right->width)
		{
			return ConcatExpr::create(
				ExtractExpr::create(
					ee_left->expr,
					ee_right->offset,
					ee_left->width+ee_right->width),
				con_right->right);
		}
	}

	return NULL;
}

/// Shortcut to concat N kids.  The chain returned is unbalanced to the right
ref<Expr> ConcatExpr::createN(unsigned n_kids, const ref<Expr> kids[])
{
	assert(n_kids > 0);

	if (n_kids == 1) return kids[0];

	ref<Expr> r = ConcatExpr::create(kids[n_kids-2], kids[n_kids-1]);
	for (int i=n_kids-3; i>=0; i--)
		r = ConcatExpr::create(kids[i], r);
	return r;
}

/// Shortcut to concat 4 kids.  The chain returned is unbalanced to the right
ref<Expr> ConcatExpr::create4(
	const ref<Expr> &kid1, const ref<Expr> &kid2,
	const ref<Expr> &kid3, const ref<Expr> &kid4)
{
	return ConcatExpr::create(
		kid1,
		ConcatExpr::create(kid2, ConcatExpr::create(kid3, kid4)));
}

/// Shortcut to concat 8 kids.  The chain returned is unbalanced to the right
ref<Expr> ConcatExpr::create8(
	const ref<Expr> &kid1, const ref<Expr> &kid2,
	const ref<Expr> &kid3, const ref<Expr> &kid4,
	const ref<Expr> &kid5, const ref<Expr> &kid6,
	const ref<Expr> &kid7, const ref<Expr> &kid8)
{
	return ConcatExpr::create(
		kid1,
		ConcatExpr::create(
			kid2,
			ConcatExpr::create(
				kid3,
				ConcatExpr::create(
					kid4,
					ConcatExpr::create4(
					kid5, kid6, kid7, kid8)))));
}

#define BOOTH_ONES_BEGIN	0
#define BOOTH_ONES_END		1
#define BOOTH_CONTINUE		2

/* quick and dirty implementation of the dumbest Booth multiplication
 * possible -- works for e->getWidth() > 64, unlike shiftaddmul. */
ref<Expr> Expr::createBoothMul(const ref<Expr>& e, uint64_t v)
{
	ref<Expr>	cur_expr;
	uint64_t	e_w;
	int		last_bit, bit_run_state;

	e_w = e->getWidth();
	last_bit = 0;

	cur_expr = ConstantExpr::create(0, e_w);
	for (uint64_t k = 0; k < 64; k++) {
		ref<Expr>	cur_mul;
		int		cur_bit;

		cur_bit = (v & (((uint64_t)1) << k)) ? 1 : 0;
		if (last_bit != cur_bit) {
			bit_run_state = (cur_bit)
				? BOOTH_ONES_BEGIN
				: BOOTH_ONES_END;
		} else {
			bit_run_state = BOOTH_CONTINUE;
		}

		last_bit = cur_bit;
		if (bit_run_state == BOOTH_CONTINUE)
			continue;

		cur_mul = ShlExpr::create(
			e,
			ConstantExpr::create(k, e_w));

		if (bit_run_state == BOOTH_ONES_END) {
			cur_expr = AddExpr::create(cur_expr, cur_mul);
		} else if (bit_run_state == BOOTH_ONES_BEGIN) {
			cur_expr = SubExpr::create(cur_expr, cur_mul);
		} else {
			assert (0 == 1 && "WTF");
		}
	}

	/* cut off sign extension */
	if (e_w > 64 && last_bit) {
		cur_expr = AddExpr::create(
			cur_expr,
			ShlExpr::create(e, ConstantExpr::create(64, e_w)));

	}

	return cur_expr;
}

/* XXX: I am pretty sure this is broken. -AJR */
ref<Expr> Expr::createShiftAddMul(const ref<Expr>& expr, uint64_t v)
{
	ref<Expr>	cur_expr;
	uint64_t	fit_width, width;
	uint64_t	add, sub;

	width = expr->getWidth();
	fit_width = width;
	if (fit_width > 64) fit_width = 64;

	// expr*x == expr*(add-sub) == expr*add - expr*sub
	ComputeMultConstants64(v, add, sub);

	// legal, these would overflow completely
	add = bits64::truncateToNBits(add, fit_width);
	sub = bits64::truncateToNBits(sub, fit_width);

	cur_expr = ConstantExpr::create(0, width);

	for (int j=63; j>=0; j--) {
		uint64_t bit = 1LL << j;

		if (!((add&bit) || (sub&bit)))
			continue;

		assert(!((add&bit) && (sub&bit)) && "invalid mult constants");

		ref<Expr>	op(
			ShlExpr::create(
				expr,
				ConstantExpr::create(j, width)));

		if (add & bit) {
			AddExpr::create(cur_expr, op);
			continue;
		}

		assert ((sub & bit) != 0);
		cur_expr = SubExpr::create(cur_expr, op);
	}

	return cur_expr;
}

ref<Expr> BinaryExpr::create(Kind k, const ref<Expr> &l, const ref<Expr> &r)
{
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

/* specialized create/allocs */
ref<Expr> NotOptimizedExpr::create(ref<Expr> src)
{ return theExprBuilder->NotOptimized(src); }

ref<Expr> ReadExpr::create(const UpdateList &updates, ref<Expr> i)
{ return theExprBuilder->Read(updates, i); }

ref<Expr> SelectExpr::create(ref<Expr> c, ref<Expr> t, ref<Expr> f)
{ return theExprBuilder->Select(c, t, f); }

ref<Expr> ExtractExpr::create(ref<Expr> e, unsigned bitOff, Width w)
{ return theExprBuilder->Extract(e, bitOff, w); }

ref<Expr> NotExpr::create(const ref<Expr> &e)
{ return theExprBuilder->Not(e); }

ref<Expr> ZExtExpr::create(const ref<Expr> &e, Width w)
{ return theExprBuilder->ZExt(e, w); }

ref<Expr> SExtExpr::create(const ref<Expr> &e, Width w)
{ return theExprBuilder->SExt(e, w); }

ref<Expr> NotOptimizedExpr::alloc(const ref<Expr>& src)
{ return theExprAllocator->NotOptimized(src); }

ref<Expr> ReadExpr::alloc(const UpdateList &updates, const ref<Expr>& i)
{ return theExprAllocator->Read(updates, i); }

ref<Expr> SelectExpr::alloc(
	const ref<Expr>& c, const ref<Expr>& t, const ref<Expr>& f)
{ return theExprAllocator->Select(c, t, f); }

ref<Expr> ExtractExpr::alloc(
	const ref<Expr>& e, unsigned bitOff, Width w)
{ return theExprAllocator->Extract(e, bitOff, w); }

ref<Expr> NotExpr::alloc(const ref<Expr> &e)
{ return theExprAllocator->Not(e); }

ref<Expr> ZExtExpr::alloc(const ref<Expr> &e, Width w)
{ return theExprAllocator->ZExt(e, w); }

ref<Expr> SExtExpr::alloc(const ref<Expr> &e, Width w)
{ return theExprAllocator->SExt(e, w); }

/* generic create/allocs */
#define DECL_CREATE_BIN_EXPR(x)	\
ref<Expr> x##Expr::create(const ref<Expr> &l, const ref<Expr> &r) \
{ return theExprBuilder->x(l,r); } \
\
ref<Expr> x##Expr::alloc(const ref<Expr> &l, const ref<Expr> &r) \
{ return theExprAllocator->x(l,r); }

DECL_CREATE_BIN_EXPR(Concat)
DECL_CREATE_BIN_EXPR(Add)
DECL_CREATE_BIN_EXPR(Sub)
DECL_CREATE_BIN_EXPR(Mul)
DECL_CREATE_BIN_EXPR(UDiv)
DECL_CREATE_BIN_EXPR(SDiv)
DECL_CREATE_BIN_EXPR(URem)
DECL_CREATE_BIN_EXPR(SRem)
DECL_CREATE_BIN_EXPR(And)
DECL_CREATE_BIN_EXPR(Or)
DECL_CREATE_BIN_EXPR(Xor)
DECL_CREATE_BIN_EXPR(Shl)
DECL_CREATE_BIN_EXPR(LShr)
DECL_CREATE_BIN_EXPR(AShr)
DECL_CREATE_BIN_EXPR(Eq)
DECL_CREATE_BIN_EXPR(Ne)
DECL_CREATE_BIN_EXPR(Ult)
DECL_CREATE_BIN_EXPR(Ule)
DECL_CREATE_BIN_EXPR(Ugt)
DECL_CREATE_BIN_EXPR(Uge)
DECL_CREATE_BIN_EXPR(Slt)
DECL_CREATE_BIN_EXPR(Sle)
DECL_CREATE_BIN_EXPR(Sgt)
DECL_CREATE_BIN_EXPR(Sge)

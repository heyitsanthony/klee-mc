#include <stdio.h>
#include "klee/Expr.h"

#include "llvm/ADT/APInt.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Support/CommandLine.h"
// FIXME: We shouldn't need this once fast constant support moves into
// Core. If we need to do arithmetic, we probably want to use APInt.
#include "klee/Internal/Support/IntEvaluation.h"

#include "klee/util/ExprPPrinter.h"
#include <tr1/unordered_map>

#include <iostream>
#include <sstream>

using namespace klee;

struct hashapint
{
unsigned operator()(const llvm::APInt& a) const { return a.getHashValue(); }
};

struct apinteq
{
bool operator()(const llvm::APInt& a, const llvm::APInt& b) const
{
	if (a.getBitWidth() != b.getBitWidth()) return false;
	return a == b;
}
};

/* important to use an unordered_map instead of a map so we get O(1) access. */
typedef std::tr1::unordered_map<
	llvm::APInt,
	ref<ConstantExpr>,
	hashapint,
	apinteq> ConstantExprTab;

ConstantExprTab			const_hashtab;
static ref<ConstantExpr>	ce_smallval_tab_1[2];
static ref<ConstantExpr>	ce_smallval_tab_8[256];
static ref<ConstantExpr>	ce_smallval_tab_16[256];
static ref<ConstantExpr>	ce_smallval_tab_32[256];
static ref<ConstantExpr>	ce_smallval_tab_64[256];


ref<Expr> ConstantExpr::fromMemory(void *address, Width width) {
  switch (width) {
  default: assert(0 && "invalid type");
  case  Expr::Bool: return ConstantExpr::create(*(( uint8_t*) address), width);
  case  Expr::Int8: return ConstantExpr::create(*(( uint8_t*) address), width);
  case Expr::Int16: return ConstantExpr::create(*((uint16_t*) address), width);
  case Expr::Int32: return ConstantExpr::create(*((uint32_t*) address), width);
  case Expr::Int64: return ConstantExpr::create(*((uint64_t*) address), width);
  // FIXME: what about machines without x87 support?
  case Expr::Fl80:
    return ConstantExpr::alloc(llvm::APInt(width,
      (width+llvm::integerPartWidth-1)/llvm::integerPartWidth,
      (const uint64_t*)address));
  }
}

void ConstantExpr::toMemory(void *address) {
  switch (getWidth()) {
  default: assert(0 && "invalid type");
  case  Expr::Bool: *(( uint8_t*) address) = getZExtValue(1); break;
  case  Expr::Int8: *(( uint8_t*) address) = getZExtValue(8); break;
  case Expr::Int16: *((uint16_t*) address) = getZExtValue(16); break;
  case Expr::Int32: *((uint32_t*) address) = getZExtValue(32); break;
  case Expr::Int64: *((uint64_t*) address) = getZExtValue(64); break;
  // FIXME: what about machines without x87 support?
  case Expr::Fl80:
    *((long double*) address) = *(long double*) value.getRawData();
    break;
  }
}

void ConstantExpr::initSmallValTab(void)
{
	ce_smallval_tab_1[0] = ref<ConstantExpr>(
		new ConstantExpr(llvm::APInt(1, 0)));
	ce_smallval_tab_1[1] = ref<ConstantExpr>(
		new ConstantExpr(llvm::APInt(1, 1)));

	ce_smallval_tab_1[0]->computeHash();
	ce_smallval_tab_1[1]->computeHash();

#define SET_SMALLTAB(w)	\
	for (unsigned int i = 0; i < 256; i++) {	\
		ref<ConstantExpr>	r(new ConstantExpr(llvm::APInt(w, i)));	\
		ce_smallval_tab_##w[i] = r;	\
		r->computeHash();		\
	}

	SET_SMALLTAB(8)
	SET_SMALLTAB(16)
	SET_SMALLTAB(32)
	SET_SMALLTAB(64)
}

static bool tab_ok = false;
ref<ConstantExpr> ConstantExpr::alloc(const llvm::APInt &v)
{
	ConstantExprTab::iterator	it;
	uint64_t			v_64;

	if (v.getBitWidth() <= 64 && (v_64 = v.getLimitedValue()) < 256) {
		if (tab_ok == false) {
			initSmallValTab();
			tab_ok = true;
		}

		switch (v.getBitWidth()) {
		case 1: return ce_smallval_tab_1[v_64];
		case 8: return ce_smallval_tab_8[v_64];
		case 16: return ce_smallval_tab_16[v_64];
		case 32: return ce_smallval_tab_32[v_64];
		case 64: return ce_smallval_tab_64[v_64];
		default: break;
		}
	}

	it = const_hashtab.find(v);
	if (it != const_hashtab.end()) {
		return it->second;
	}

	ref<ConstantExpr> r(new ConstantExpr(v));
	r->computeHash();
	const_hashtab.insert(std::make_pair(v, r));

	return r;
}

void ConstantExpr::toString(std::string &Res) const {
  Res = value.toString(10, false);
}

/* N.B. vector is stored *backwards* (i.e. v[0] => cur_v[w - 1]) */
ref<ConstantExpr> ConstantExpr::createVector(llvm::ConstantVector* v)
{
	unsigned int	elem_count;

	elem_count = v->getNumOperands();

	ref<ConstantExpr>	cur_v;
	for (unsigned int i = 0; i < elem_count; i++) {
		llvm::ConstantInt *cur_ci;

		cur_ci = dyn_cast<llvm::ConstantInt>(v->getOperand(i));
		assert (cur_ci != NULL);

		if (i == 0) cur_v = alloc(cur_ci->getValue());
		else cur_v = cur_v->Concat(alloc(cur_ci->getValue()));
	}

	return cur_v;
}

ref<ConstantExpr> ConstantExpr::Concat(const ref<ConstantExpr> &RHS) 
{
  Expr::Width W = getWidth() + RHS->getWidth();
  llvm::APInt Tmp(value);
  Tmp.zext(W);
  Tmp <<= RHS->getWidth();
  Tmp |= llvm::APInt(RHS->value).zext(W);

  return ConstantExpr::alloc(Tmp);
}

ref<ConstantExpr> ConstantExpr::Extract(unsigned Offset, Width W) {
  return ConstantExpr::alloc(llvm::APInt(value.ashr(Offset)).zextOrTrunc(W));
}

ref<ConstantExpr> ConstantExpr::ZExt(Width W) {
  return ConstantExpr::alloc(llvm::APInt(value).zextOrTrunc(W));
}

ref<ConstantExpr> ConstantExpr::SExt(Width W) {
  return ConstantExpr::alloc(llvm::APInt(value).sextOrTrunc(W));
}

#define DECL_CE_OP(FNAME, OP)	\
ref<ConstantExpr> ConstantExpr::FNAME(const ref<ConstantExpr> &in_rhs) \
{ return ConstantExpr::alloc(value OP in_rhs->value); }

#define DECL_CE_FUNCOP(FNAME, OP)	\
ref<ConstantExpr> ConstantExpr::FNAME(const ref<ConstantExpr> &in_rhs) \
{ return ConstantExpr::alloc(value.OP(in_rhs->value)); }

#define DECL_CE_CMPOP(FNAME, OP)	\
ref<ConstantExpr> ConstantExpr::FNAME(const ref<ConstantExpr> &in_rhs)	\
{ return ConstantExpr::alloc(value.OP(in_rhs->value), Expr::Bool); }

DECL_CE_OP(Add, +)
DECL_CE_OP(Sub, -)
DECL_CE_OP(Mul, *)
DECL_CE_OP(And, &)
DECL_CE_OP(Or, |)
DECL_CE_OP(Xor, ^)
DECL_CE_FUNCOP(UDiv, udiv)
DECL_CE_FUNCOP(SDiv, sdiv)
DECL_CE_FUNCOP(URem, urem)
DECL_CE_FUNCOP(SRem, srem)
DECL_CE_FUNCOP(Shl, shl)
DECL_CE_FUNCOP(LShr, lshr)
DECL_CE_FUNCOP(AShr, ashr)

ref<ConstantExpr> ConstantExpr::Neg() { return ConstantExpr::alloc(-value); }
ref<ConstantExpr> ConstantExpr::Not() {  return ConstantExpr::alloc(~value); }

ref<ConstantExpr> ConstantExpr::Eq(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value == RHS->value, Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::Ne(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value != RHS->value, Expr::Bool);
}

DECL_CE_CMPOP(Ult, ult)
DECL_CE_CMPOP(Ule, ule)
DECL_CE_CMPOP(Ugt, ugt)
DECL_CE_CMPOP(Uge, uge)
DECL_CE_CMPOP(Slt, slt)
DECL_CE_CMPOP(Sle, sle)
DECL_CE_CMPOP(Sgt, sgt)
DECL_CE_CMPOP(Sge, sge)

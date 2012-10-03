#include <stdio.h>
#include "klee/Expr.h"

#include "ExprAlloc.h"

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

void ConstantExpr::toString(std::string &Res) const
{ Res = value.toString(10, false); }


ref<ConstantExpr> ConstantExpr::alloc(const llvm::APInt &v)
{ return cast<ConstantExpr>(theExprAllocator->Constant(v)); }

/* N.B. vector is stored *backwards* (i.e. v[0] => cur_v[w - 1]) */
ref<ConstantExpr> ConstantExpr::createVector(llvm::ConstantVector* v)
{
	unsigned int	elem_count;

	elem_count = v->getNumOperands();

	ref<ConstantExpr>	cur_v;
	for (unsigned int i = 0; i < elem_count; i++) {
		llvm::ConstantInt	*cur_ci;
		llvm::ConstantFP	*cur_fi;
		llvm::APInt		api;

		cur_ci = dyn_cast<llvm::ConstantInt>(v->getOperand(i));
		cur_fi = dyn_cast<llvm::ConstantFP>(v->getOperand(i));
		if (cur_ci != NULL) {
			api = cur_ci->getValue();
		} else if (cur_fi != NULL) {
			api = cur_fi->getValueAPF().bitcastToAPInt();
		} else {
			assert (0 == 1 && "Weird type??");
		}

		if (i == 0) cur_v = alloc(api);
		else cur_v = cur_v->Concat(alloc(api));
	}

	return cur_v;
}

ref<ConstantExpr> ConstantExpr::createSeqData(llvm::ConstantDataSequential* v)
{
	unsigned		bytes_per_elem, elem_c;
	ref<ConstantExpr>	cur_v;

	bytes_per_elem = v->getElementByteSize();
	elem_c = v->getNumElements();

	assert (bytes_per_elem*elem_c <= 64);
	assert (isa<llvm::IntegerType>(v->getElementType()));

	for (unsigned i = 0; i < elem_c; i++) {
		ref<ConstantExpr>	ce;
		ce = ConstantExpr::create(
			v->getElementAsInteger(i),
			bytes_per_elem*8);

		if (i == 0) cur_v = ce;
		else cur_v = cur_v->Concat(ce);
	}


	return cur_v;
}

ref<ConstantExpr> ConstantExpr::Concat(const ref<ConstantExpr> &RHS)
{
  Expr::Width W = getWidth() + RHS->getWidth();
  llvm::APInt Tmp(value);
  Tmp = Tmp.zext(W);
  Tmp <<= RHS->getWidth();
  Tmp |= llvm::APInt(RHS->value).zext(W);

  return ConstantExpr::alloc(Tmp);
}

ref<ConstantExpr> ConstantExpr::Extract(unsigned Offset, Width W) const
{ return ConstantExpr::alloc(llvm::APInt(value.ashr(Offset)).zextOrTrunc(W)); }

ref<ConstantExpr> ConstantExpr::ZExt(Width W)
{ return ConstantExpr::alloc(llvm::APInt(value).zextOrTrunc(W)); }

ref<ConstantExpr> ConstantExpr::SExt(Width W)
{ return ConstantExpr::alloc(llvm::APInt(value).sextOrTrunc(W)); }

#define DECL_CE_OP(FNAME, OP)	\
ref<ConstantExpr> ConstantExpr::FNAME(const ref<ConstantExpr> &in_rhs) \
{ return ConstantExpr::alloc(value OP in_rhs->value); }

#define DECL_CE_FUNCOP(FNAME, OP)	\
ref<ConstantExpr> ConstantExpr::FNAME(const ref<ConstantExpr> &in_rhs) \
{ return ConstantExpr::alloc(value.OP(in_rhs->value)); }

#define DECL_CE_NONZERO_FUNCOP(FNAME, OP)	\
ref<ConstantExpr> ConstantExpr::FNAME(const ref<ConstantExpr> &in_rhs) \
{ \
	if (in_rhs->isZero()) {	\
		if (!Expr::errors)	\
			std::cerr << "[Expr] 0 as RHS on restricted op\n"; \
		Expr::errors++;	\
		return in_rhs;	\
	} \
	return ConstantExpr::alloc(value.OP(in_rhs->value));	\
}


#define DECL_CE_CMPOP(FNAME, OP)	\
ref<ConstantExpr> ConstantExpr::FNAME(const ref<ConstantExpr> &in_rhs)	\
{ return ConstantExpr::alloc(value.OP(in_rhs->value), Expr::Bool); }

DECL_CE_OP(Add, +)
DECL_CE_OP(Sub, -)
DECL_CE_OP(Mul, *)
DECL_CE_OP(And, &)
DECL_CE_OP(Or, |)
DECL_CE_OP(Xor, ^)
DECL_CE_NONZERO_FUNCOP(UDiv, udiv)
DECL_CE_NONZERO_FUNCOP(SDiv, sdiv)
DECL_CE_NONZERO_FUNCOP(URem, urem)
DECL_CE_NONZERO_FUNCOP(SRem, srem)
DECL_CE_FUNCOP(Shl, shl)
DECL_CE_FUNCOP(LShr, lshr)

ref<ConstantExpr> ConstantExpr::AShr(const ref<ConstantExpr> &in_rhs)
{
	if (getWidth() == 1) return this;
	return ConstantExpr::alloc(value.ashr(in_rhs->value));
}

ref<ConstantExpr> ConstantExpr::Neg() { return ConstantExpr::alloc(-value); }
ref<ConstantExpr> ConstantExpr::Not() { return ConstantExpr::alloc(~value); }

ref<ConstantExpr> ConstantExpr::Eq(const ref<ConstantExpr> &RHS)
{ return ConstantExpr::alloc(value == RHS->value, Expr::Bool); }

ref<ConstantExpr> ConstantExpr::Ne(const ref<ConstantExpr> &RHS)
{ return ConstantExpr::alloc(value != RHS->value, Expr::Bool); }

DECL_CE_CMPOP(Ult, ult)
DECL_CE_CMPOP(Ule, ule)
DECL_CE_CMPOP(Ugt, ugt)
DECL_CE_CMPOP(Uge, uge)
DECL_CE_CMPOP(Slt, slt)
DECL_CE_CMPOP(Sle, sle)
DECL_CE_CMPOP(Sgt, sgt)
DECL_CE_CMPOP(Sge, sge)

#include <llvm/ADT/Hashing.h>

Expr::Hash ConstantExpr::computeHash(void)
{
	skeletonHash = getWidth() * MAGIC_HASH_CONSTANT;
	hashValue =
		//value.hash_value(value).size_t() 
		hash_value(value)
		^ (getWidth() * MAGIC_HASH_CONSTANT);
	return hashValue;
}

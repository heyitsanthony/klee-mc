//===-- Context.cpp -------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Context.h"

#include "klee/Expr.h"

#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"

#include <cassert>

using namespace klee;

static bool Initialized = false;
Context Context::TheContext;

void Context::initialize(bool IsLittleEndian, Expr::Width PointerWidth) {
  assert(!Initialized && "Duplicate context initialization!");
  TheContext = Context(IsLittleEndian, PointerWidth);
  Initialized = true;
}

ref<Expr> Expr::createCoerceToPointerType(const ref<Expr>& e)
{ return MK_ZEXT(e, Context::get().getPointerWidth()); }

ref<ConstantExpr> Expr::createPointer(uint64_t v)
{  return MK_CONST(v, Context::get().getPointerWidth()); }

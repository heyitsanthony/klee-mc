#include "BitfieldSimplifierBuilder.h"

using namespace klee;

#define BSF_DECL_BIN_REF(x)	\
ref<Expr> BitfieldSimplifierBuilder::x(		\
	const ref<Expr> &LHS, const ref<Expr> &RHS)	\
{ return simplify(eb->x(LHS, RHS)); }

ref<Expr> BitfieldSimplifierBuilder::Constant(const llvm::APInt &Value)
{ return eb->Constant(Value); }

ref<Expr> BitfieldSimplifierBuilder::NotOptimized(const ref<Expr> &Index)
{ return eb->NotOptimized(Index); }

ref<Expr> BitfieldSimplifierBuilder::Read(
	const UpdateList &Updates, const ref<Expr> &idx)
{ return simplify(eb->Read(Updates, idx)); }

ref<Expr> BitfieldSimplifierBuilder::Select(
		const ref<Expr> &Cond,	
		const ref<Expr> &LHS, const ref<Expr> &RHS)
{ return simplify(eb->Select(Cond, LHS, RHS)); }

ref<Expr> BitfieldSimplifierBuilder::Extract(	
		const ref<Expr> &LHS, unsigned Offset, Expr::Width W)
{ return simplify(eb->Extract(LHS, Offset, W)); }

ref<Expr> BitfieldSimplifierBuilder::ZExt(const ref<Expr> &LHS, Expr::Width W)
{ return simplify(eb->ZExt(LHS, W)); }

ref<Expr> BitfieldSimplifierBuilder::SExt(const ref<Expr> &LHS, Expr::Width W)
{ return simplify(eb->SExt(LHS, W)); }

ref<Expr> BitfieldSimplifierBuilder::Not(const ref<Expr> &LHS)
{ return simplify(eb->Not(LHS)); }

BSF_DECL_BIN_REF(Concat)	
BSF_DECL_BIN_REF(Add)	
BSF_DECL_BIN_REF(Sub)	
BSF_DECL_BIN_REF(Mul)	
BSF_DECL_BIN_REF(UDiv)	

BSF_DECL_BIN_REF(SDiv)	
BSF_DECL_BIN_REF(URem)	
BSF_DECL_BIN_REF(SRem)	
BSF_DECL_BIN_REF(And)	
BSF_DECL_BIN_REF(Or)	
BSF_DECL_BIN_REF(Xor)	
BSF_DECL_BIN_REF(Shl)	
BSF_DECL_BIN_REF(LShr)	
BSF_DECL_BIN_REF(AShr)	
BSF_DECL_BIN_REF(Eq)	
BSF_DECL_BIN_REF(Ne)	

BSF_DECL_BIN_REF(Ult)	
BSF_DECL_BIN_REF(Ule)	
BSF_DECL_BIN_REF(Ugt)	
BSF_DECL_BIN_REF(Uge)	
BSF_DECL_BIN_REF(Slt)	
BSF_DECL_BIN_REF(Sle)	
BSF_DECL_BIN_REF(Sgt)	
BSF_DECL_BIN_REF(Sge)


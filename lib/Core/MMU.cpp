#include <llvm/IR/DataLayout.h>
#include <llvm/Support/CommandLine.h>

#include "Executor.h"
#include "MMU.h"
#include "SoftConcreteMMU.h"
#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"

using namespace klee;

uint64_t klee::MMU::query_c = 0;
uint64_t klee::MMU::sym_r_c = 0;
uint64_t klee::MMU::sym_w_c = 0;

extern unsigned MakeConcreteSymbolic;

namespace {
	llvm::cl::opt<bool>
	UseInstMMU(
		"use-inst-mmu",
		llvm::cl::desc("MMU with forwarding instrumentation."),
		llvm::cl::init(false));

	llvm::cl::opt<bool>
	UseSymMMU(
		"use-sym-mmu",
		llvm::cl::desc("Use MMU that forwards to interpreter."),
		llvm::cl::init(true));

	llvm::cl::opt<bool>
	OnlySymMMU(
		"use-only-sym-mmu",
		llvm::cl::desc("Do not use concrete address fast-path."),
		llvm::cl::init(false));
};

void MMU::MemOp::simplify(ExecutionState& state)
{
	if (!isa<ConstantExpr>(address))
		address = state.constraints.simplifyExpr(address);
	if (isWrite && !isa<ConstantExpr>(value))
		value = state.constraints.simplifyExpr(value);
}

Expr::Width MMU::MemOp::getType(const KModule* m) const
{
	if (type_cache != -1)
		return type_cache;

	type_cache = (isWrite
		? value->getWidth()
		: m->dataLayout->getTypeSizeInBits(
			target->getInst()->getType()));

	return type_cache;
}

ref<Expr> MMU::readDebug(ExecutionState& state, uint64_t addr)
{
	ObjectPair	op;
	uint64_t	off;
	bool		found;

	found = state.addressSpace.resolveOne(addr, op);
	if (found == false)
		return NULL;

	off = addr - op.first->address;
	return state.read(op.second, off, 64);
}


/* TODO: make this a command line option */
#include "DualMMU.h"
#include "KleeMMU.h"
#include "InstMMU.h"
MMU* MMU::create(Executor& exe)
{
	/* XXX should have concrete mmu be able to do this */
	if (MakeConcreteSymbolic)
		return new KleeMMU(exe);

	if (UseInstMMU)
		return new InstMMU(exe);

	return new DualMMU(exe);
}

bool MMU::isSymMMU(void) { return UseSymMMU; }

bool MMU::isSoftConcreteMMU(void)
{ return !SoftConcreteMMU::getType().empty(); }

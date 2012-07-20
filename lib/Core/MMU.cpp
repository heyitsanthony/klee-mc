#include <llvm/Target/TargetData.h>

#include "Executor.h"
#include "MMU.h"
#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"

using namespace klee;

uint64_t klee::MMU::query_c = 0;

extern unsigned MakeConcreteSymbolic;

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
		: m->targetData->getTypeSizeInBits(
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
MMU* MMU::create(Executor& exe)
{
	/* XXX should have concrete mmu be able to do this */
	if (MakeConcreteSymbolic)
		return new KleeMMU(exe);
	return new DualMMU(exe);
}

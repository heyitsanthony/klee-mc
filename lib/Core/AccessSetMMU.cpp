#include "AccessSetMMU.h"

using namespace klee;

bool AccessSetMMU::exeMemOp(ExecutionState &state, MemOp& mop)
{
	if (!mop.target) {
		no_target_c++;
		return base_mmu.exeMemOp(state, mop);
	}

	auto	&addrset = ki2set[mop.target];
	addrset.insert(mop.address);

	return base_mmu.exeMemOp(state, mop);
}
